#include <dav1d/dav1d.h>
#include <errno.h>
#include <cstring>
#include "video_decoder.h"

void VideoDecoder::init_dav1d_decoder() {
  Dav1dSettings s;
  dav1d_default_settings(&s);
  // スレッド数を1に設定してdav1d内部のマルチスレッドを無効化
  // （既にワーカースレッドがあるため）
  s.n_threads = 1;
  s.max_frame_delay = 1;
  s.operating_point = 0;  // すべてのレイヤーをデコード
  s.all_layers = 1;       // すべてのレイヤーを出力

  Dav1dContext* ctx = nullptr;
  if (dav1d_open(&ctx, &s) < 0) {
    throw std::runtime_error("Failed to initialize dav1d decoder");
  }
  decoder_context_ = ctx;
}

void VideoDecoder::cleanup_dav1d_decoder() {
  // ワーカースレッドが完全に停止していることを確認
  // (close() から呼ばれる前に stop_worker() が呼ばれているはず)
  if (worker_thread_.joinable()) {
    // ワーカースレッドがまだ動いている場合は、エラー
    return;
  }

  if (decoder_context_) {
    Dav1dContext* ctx = static_cast<Dav1dContext*>(decoder_context_);
    // 残っているフレームを全てフラッシュ
    Dav1dPicture pic = {};
    while (dav1d_get_picture(ctx, &pic) >= 0) {
      dav1d_picture_unref(&pic);
    }
    // コンテキストをクローズ
    // dav1d_close は Dav1dContext** を受け取り、ポインタを NULL にセットする
    Dav1dContext** ctx_ptr =
        reinterpret_cast<Dav1dContext**>(&decoder_context_);
    dav1d_close(ctx_ptr);
    decoder_context_ = nullptr;
  }
}

bool VideoDecoder::decode_dav1d(const EncodedVideoChunk& chunk) {
  // デコーダーコンテキストの有効性をチェック
  if (!decoder_context_) {
    return false;
  }

  Dav1dContext* ctx = static_cast<Dav1dContext*>(decoder_context_);
  if (!ctx) {
    return false;
  }

  auto pkt = chunk.data_vector();
  Dav1dData data = {};
  uint8_t* buf = (uint8_t*)malloc(pkt.size());
  if (!buf)
    return false;
  memcpy(buf, pkt.data(), pkt.size());
  if (dav1d_data_wrap(
          &data, buf, pkt.size(),
          [](const uint8_t* p, void* cookie) { free((void*)p); },
          nullptr) < 0) {
    free(buf);
    return false;
  }

  // バインディング層で既に GIL を解放しているため、ここでは解放しない
  // nb::gil_scoped_release gil_release;

  int r = dav1d_send_data(ctx, &data);
  if (r < 0 && r != -EAGAIN) {
    // エラーが発生した場合、残っているデータをクリーンアップ
    dav1d_data_unref(&data);
    return false;
  }
  Dav1dPicture pic = {};
  bool got = false;
  while (true) {
    r = dav1d_get_picture(ctx, &pic);
    if (r == -EAGAIN) {
      // もうフレームがない
      break;
    }
    if (r < 0) {
      // エラーが発生した場合も picture を解放する必要がある
      if (pic.data[0]) {
        dav1d_picture_unref(&pic);
      }
      break;
    }
    got = true;

    // 有効な画像データがあるか確認
    if (pic.p.w > 0 && pic.p.h > 0 && pic.data[0] && pic.data[1] &&
        pic.data[2] && pic.p.bpc == 8 &&
        pic.p.layout == DAV1D_PIXEL_LAYOUT_I420) {
      // サイズが極端に大きくないかチェック
      if (pic.p.w > 8192 || pic.p.h > 8192) {
        // 異常なサイズの場合はスキップ
        continue;
      }

      // VideoFrame を作成
      auto frame = std::make_unique<VideoFrame>(
          pic.p.w, pic.p.h, VideoPixelFormat::I420, chunk.timestamp());

      // mutable_plane_ptr を使って直接データをコピー（GIL 不要）
      const uint32_t frame_width = frame->width();
      const uint32_t frame_height = frame->height();
      const uint32_t chroma_width = frame_width / 2;
      const uint32_t chroma_height = frame_height / 2;

      // Y プレーン
      uint8_t* y_dst = frame->mutable_plane_ptr(0);
      for (uint32_t row = 0; row < frame_height; ++row) {
        memcpy(y_dst + row * frame_width,
               (uint8_t*)pic.data[0] + row * pic.stride[0], frame_width);
      }

      // U プレーン
      uint8_t* u_dst = frame->mutable_plane_ptr(1);
      for (uint32_t row = 0; row < chroma_height; ++row) {
        memcpy(u_dst + row * chroma_width,
               (uint8_t*)pic.data[1] + row * pic.stride[1], chroma_width);
      }

      // V プレーン（I420 では U と V は同じストライド stride[1] を使用）
      uint8_t* v_dst = frame->mutable_plane_ptr(2);
      for (uint32_t row = 0; row < chroma_height; ++row) {
        memcpy(v_dst + row * chroma_width,
               (uint8_t*)pic.data[2] + row * pic.stride[1],  // stride[1] を使用
               chroma_width);
      }

      frame->set_duration(chunk.duration());

      // 順序制御された出力処理（GIL を再取得せずに実行）
      handle_output(current_sequence_, std::move(frame));
    }

    // 必ず picture を解放
    dav1d_picture_unref(&pic);
  }
  return got;
}