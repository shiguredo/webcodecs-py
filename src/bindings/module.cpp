#include <nanobind/nanobind.h>

namespace nb = nanobind;

// バインディング関数の前方宣言
void init_webcodecs_types(nb::module_& m);
void init_video_frame(nb::module_& m);
void init_audio_data(nb::module_& m);
void init_encoded_video_chunk(nb::module_& m);
void init_encoded_audio_chunk(nb::module_& m);
void init_video_decoder(nb::module_& m);
void init_audio_decoder(nb::module_& m);
void init_video_encoder(nb::module_& m);
void init_audio_encoder(nb::module_& m);
void init_video_codec_capabilities(nb::module_& m);
void init_image_decoder(nb::module_& m);

NB_MODULE(_webcodecs_py, m) {
  m.doc() = "Python bindings for media codecs with WebCodecs-like API";

  // 全てのサブモジュールを初期化
  init_webcodecs_types(m);  // WebCodecs 型を先に初期化
  init_video_frame(m);
  init_audio_data(m);
  init_encoded_video_chunk(m);
  init_encoded_audio_chunk(m);
  init_video_decoder(m);
  init_audio_decoder(m);
  init_video_encoder(m);
  init_audio_encoder(m);
  init_video_codec_capabilities(m);
  init_image_decoder(m);
}