#include "webcodecs_types.h"
#include <nanobind/stl/optional.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>
#include <stdexcept>
#include "video_frame.h"  // VideoPixelFormat の定義のため

using namespace nb::literals;

void VideoFrameBufferInit::validate() const {
  if (format.empty()) {
    throw std::invalid_argument("format is required");
  }
  if (coded_width == 0) {
    throw std::invalid_argument("coded_width is required");
  }
  if (coded_height == 0) {
    throw std::invalid_argument("coded_height is required");
  }
  // timestamp は負の値でも許可する（WebCodecs 仕様）
}

void init_webcodecs_types(nb::module_& m) {
  // CodecState 列挙型
  nb::enum_<CodecState>(m, "CodecState")
      .value("UNCONFIGURED", CodecState::UNCONFIGURED)
      .value("CONFIGURED", CodecState::CONFIGURED)
      .value("CLOSED", CodecState::CLOSED);

  // LatencyMode 列挙型
  nb::enum_<LatencyMode>(m, "LatencyMode")
      .value("QUALITY", LatencyMode::QUALITY)
      .value("REALTIME", LatencyMode::REALTIME);

  // VideoEncoderBitrateMode 列挙型
  nb::enum_<VideoEncoderBitrateMode>(m, "VideoEncoderBitrateMode")
      .value("CONSTANT", VideoEncoderBitrateMode::CONSTANT)
      .value("VARIABLE", VideoEncoderBitrateMode::VARIABLE)
      .value("QUANTIZER", VideoEncoderBitrateMode::QUANTIZER);

  // BitrateMode 列挙型 (AudioEncoder 用)
  nb::enum_<BitrateMode>(m, "BitrateMode")
      .value("CONSTANT", BitrateMode::CONSTANT)
      .value("VARIABLE", BitrateMode::VARIABLE);

  // AlphaOption 列挙型
  nb::enum_<AlphaOption>(m, "AlphaOption")
      .value("KEEP", AlphaOption::KEEP)
      .value("DISCARD", AlphaOption::DISCARD);

  // HardwareAcceleration 列挙型
  nb::enum_<HardwareAcceleration>(m, "HardwareAcceleration")
      .value("NO_PREFERENCE", HardwareAcceleration::NO_PREFERENCE)
      .value("PREFER_HARDWARE", HardwareAcceleration::PREFER_HARDWARE)
      .value("PREFER_SOFTWARE", HardwareAcceleration::PREFER_SOFTWARE);

  // VideoColorPrimaries 列挙型
  nb::enum_<VideoColorPrimaries>(m, "VideoColorPrimaries")
      .value("BT709", VideoColorPrimaries::BT709)
      .value("BT470BG", VideoColorPrimaries::BT470BG)
      .value("SMPTE170M", VideoColorPrimaries::SMPTE170M)
      .value("BT2020", VideoColorPrimaries::BT2020)
      .value("SMPTE432", VideoColorPrimaries::SMPTE432);

  // VideoTransferCharacteristics 列挙型
  nb::enum_<VideoTransferCharacteristics>(m, "VideoTransferCharacteristics")
      .value("BT709", VideoTransferCharacteristics::BT709)
      .value("SMPTE170M", VideoTransferCharacteristics::SMPTE170M)
      .value("IEC61966_2_1", VideoTransferCharacteristics::IEC61966_2_1)
      .value("LINEAR", VideoTransferCharacteristics::LINEAR)
      .value("PQ", VideoTransferCharacteristics::PQ)
      .value("HLG", VideoTransferCharacteristics::HLG);

  // VideoMatrixCoefficients 列挙型
  nb::enum_<VideoMatrixCoefficients>(m, "VideoMatrixCoefficients")
      .value("RGB", VideoMatrixCoefficients::RGB)
      .value("BT709", VideoMatrixCoefficients::BT709)
      .value("BT470BG", VideoMatrixCoefficients::BT470BG)
      .value("SMPTE170M", VideoMatrixCoefficients::SMPTE170M)
      .value("BT2020_NCL", VideoMatrixCoefficients::BT2020_NCL);

  // PlaneLayout クラス
  nb::class_<PlaneLayout>(m, "PlaneLayout")
      .def(nb::init<>())
      .def(nb::init<uint32_t, uint32_t>(), "offset"_a, "stride"_a)
      .def_rw("offset", &PlaneLayout::offset)
      .def_rw("stride", &PlaneLayout::stride);

  // DOMRect クラス
  nb::class_<DOMRect>(m, "DOMRect")
      .def(nb::init<>())
      .def(nb::init<double, double, double, double>(), "x"_a, "y"_a, "width"_a,
           "height"_a)
      .def_rw("x", &DOMRect::x)
      .def_rw("y", &DOMRect::y)
      .def_rw("width", &DOMRect::width)
      .def_rw("height", &DOMRect::height);

  // VideoColorSpace クラス
  nb::class_<VideoColorSpace>(m, "VideoColorSpace")
      .def(nb::init<>())
      .def_rw("primaries", &VideoColorSpace::primaries)
      .def_rw("transfer", &VideoColorSpace::transfer)
      .def_rw("matrix", &VideoColorSpace::matrix)
      .def_rw("full_range", &VideoColorSpace::full_range);

  // VideoFrameBufferInit は Python 側で TypedDict として定義されているため、C++
  // 側では公開しない

  // VideoEncoderConfig, VideoDecoderConfig, AudioEncoderConfig, AudioDecoderConfig は
  // Python 側で TypedDict として定義されているため、C++ 側では公開しない
  // これらは内部的にのみ使用される
  /*
  // VideoEncoderConfig クラス
  nb::class_<VideoEncoderConfig>(m, "VideoEncoderConfig")
      .def(nb::init<>())
      .def("__init__",
           [](VideoEncoderConfig* self, nb::kwargs kwargs) {
             new (self) VideoEncoderConfig();

             // 必須パラメータのチェック
             if (!kwargs.contains("codec")) {
               throw nb::value_error("codec is required");
             }
             if (!kwargs.contains("width")) {
               throw nb::value_error("width is required");
             }
             if (!kwargs.contains("height")) {
               throw nb::value_error("height is required");
             }

             // キーワード引数から値を設定
             self->codec = nb::cast<std::string>(kwargs["codec"]);
             self->width = nb::cast<uint32_t>(kwargs["width"]);
             self->height = nb::cast<uint32_t>(kwargs["height"]);

             if (kwargs.contains("display_width")) {
               self->display_width =
                   nb::cast<uint32_t>(kwargs["display_width"]);
             }
             if (kwargs.contains("display_height")) {
               self->display_height =
                   nb::cast<uint32_t>(kwargs["display_height"]);
             }
             if (kwargs.contains("bitrate")) {
               self->bitrate = nb::cast<uint64_t>(kwargs["bitrate"]);
             }
             if (kwargs.contains("framerate")) {
               self->framerate = nb::cast<double>(kwargs["framerate"]);
             }
             if (kwargs.contains("hardware_acceleration")) {
               self->hardware_acceleration =
                   nb::cast<std::string>(kwargs["hardware_acceleration"]);
             }
             if (kwargs.contains("alpha")) {
               self->alpha = nb::cast<std::string>(kwargs["alpha"]);
             }
             if (kwargs.contains("scalability_mode")) {
               self->scalability_mode =
                   nb::cast<std::string>(kwargs["scalability_mode"]);
             }
             if (kwargs.contains("bitrate_mode")) {
               self->bitrate_mode =
                   nb::cast<std::string>(kwargs["bitrate_mode"]);
             }
             if (kwargs.contains("latency_mode")) {
               self->latency_mode =
                   nb::cast<std::string>(kwargs["latency_mode"]);
             }
             if (kwargs.contains("content_hint")) {
               self->content_hint =
                   nb::cast<std::string>(kwargs["content_hint"]);
             }
           })
      .def_rw("codec", &VideoEncoderConfig::codec)
      .def_rw("width", &VideoEncoderConfig::width)
      .def_rw("height", &VideoEncoderConfig::height)
      .def_rw("display_width", &VideoEncoderConfig::display_width)
      .def_rw("display_height", &VideoEncoderConfig::display_height)
      .def_rw("bitrate", &VideoEncoderConfig::bitrate)
      .def_rw("framerate", &VideoEncoderConfig::framerate)
      .def_rw("alpha", &VideoEncoderConfig::alpha)
      .def_rw("scalability_mode", &VideoEncoderConfig::scalability_mode)
      .def_rw("bitrate_mode", &VideoEncoderConfig::bitrate_mode)
      .def_rw("latency_mode", &VideoEncoderConfig::latency_mode)
      .def_rw("content_hint", &VideoEncoderConfig::content_hint)
      .def_rw("hardware_acceleration",
              &VideoEncoderConfig::hardware_acceleration)
      .def_rw("hardware_acceleration_engine",
              &VideoEncoderConfig::hardware_acceleration_engine);

  // VideoDecoderConfig クラス
  nb::class_<VideoDecoderConfig>(m, "VideoDecoderConfig")
      .def(nb::init<>())
      .def("__init__",
           [](VideoDecoderConfig* self, nb::kwargs kwargs) {
             new (self) VideoDecoderConfig();

             // 必須パラメータのチェック
             if (!kwargs.contains("codec")) {
               throw nb::value_error("codec is required");
             }

             // キーワード引数から値を設定
             self->codec = nb::cast<std::string>(kwargs["codec"]);

             if (kwargs.contains("description")) {
               nb::bytes desc = nb::cast<nb::bytes>(kwargs["description"]);
               const char* ptr = desc.c_str();
               size_t size = desc.size();
               self->description = std::vector<uint8_t>(
                   reinterpret_cast<const uint8_t*>(ptr),
                   reinterpret_cast<const uint8_t*>(ptr) + size);
             }
             if (kwargs.contains("coded_width")) {
               self->coded_width = nb::cast<uint32_t>(kwargs["coded_width"]);
             }
             if (kwargs.contains("coded_height")) {
               self->coded_height = nb::cast<uint32_t>(kwargs["coded_height"]);
             }
             if (kwargs.contains("display_aspect_width")) {
               self->display_aspect_width =
                   nb::cast<uint32_t>(kwargs["display_aspect_width"]);
             }
             if (kwargs.contains("display_aspect_height")) {
               self->display_aspect_height =
                   nb::cast<uint32_t>(kwargs["display_aspect_height"]);
             }
             if (kwargs.contains("color_space")) {
               auto cs_obj = kwargs["color_space"];
               if (nb::isinstance<nb::dict>(cs_obj)) {
                 auto cs_dict = nb::cast<nb::dict>(cs_obj);
                 VideoColorSpace cs;
                 if (cs_dict.contains("primaries"))
                   cs.primaries = nb::cast<std::string>(cs_dict["primaries"]);
                 if (cs_dict.contains("transfer"))
                   cs.transfer = nb::cast<std::string>(cs_dict["transfer"]);
                 if (cs_dict.contains("matrix"))
                   cs.matrix = nb::cast<std::string>(cs_dict["matrix"]);
                 if (cs_dict.contains("full_range"))
                   cs.full_range = nb::cast<bool>(cs_dict["full_range"]);
                 self->color_space = cs;
               } else {
                 self->color_space = nb::cast<VideoColorSpace>(cs_obj);
               }
             }
             if (kwargs.contains("hardware_acceleration_engine")) {
               self->hardware_acceleration_engine =
                   nb::cast<HardwareAccelerationEngine>(kwargs["hardware_acceleration_engine"]);
             }
             if (kwargs.contains("optimize_for_latency")) {
               self->optimize_for_latency =
                   nb::cast<bool>(kwargs["optimize_for_latency"]);
             }
             if (kwargs.contains("rotation")) {
               self->rotation = nb::cast<double>(kwargs["rotation"]);
             }
             if (kwargs.contains("flip")) {
               self->flip = nb::cast<bool>(kwargs["flip"]);
             }
           })
      .def_rw("codec", &VideoDecoderConfig::codec)
      .def_rw("description", &VideoDecoderConfig::description)
      .def_rw("coded_width", &VideoDecoderConfig::coded_width)
      .def_rw("coded_height", &VideoDecoderConfig::coded_height)
      .def_rw("display_aspect_width", &VideoDecoderConfig::display_aspect_width)
      .def_rw("display_aspect_height",
              &VideoDecoderConfig::display_aspect_height)
      .def_rw("color_space", &VideoDecoderConfig::color_space)
      .def_rw("hardware_acceleration_engine",
              &VideoDecoderConfig::hardware_acceleration_engine)
      .def_rw("optimize_for_latency", &VideoDecoderConfig::optimize_for_latency)
      .def_rw("rotation", &VideoDecoderConfig::rotation)
      .def_rw("flip", &VideoDecoderConfig::flip);

  // AudioEncoderConfig クラス
  nb::class_<AudioEncoderConfig>(m, "AudioEncoderConfig")
      .def(nb::init<>())
      .def("__init__",
           [](AudioEncoderConfig* self, nb::kwargs kwargs) {
             new (self) AudioEncoderConfig();

             // 必須パラメータのチェック
             if (!kwargs.contains("codec")) {
               throw nb::value_error("codec is required");
             }
             if (!kwargs.contains("sample_rate")) {
               throw nb::value_error("sample_rate is required");
             }
             if (!kwargs.contains("number_of_channels")) {
               throw nb::value_error("number_of_channels is required");
             }

             // キーワード引数から値を設定
             self->codec = nb::cast<std::string>(kwargs["codec"]);
             self->sample_rate = nb::cast<uint32_t>(kwargs["sample_rate"]);
             self->number_of_channels =
                 nb::cast<uint32_t>(kwargs["number_of_channels"]);

             if (kwargs.contains("bitrate")) {
               self->bitrate = nb::cast<uint64_t>(kwargs["bitrate"]);
             }
             if (kwargs.contains("bitrate_mode")) {
               self->bitrate_mode =
                   nb::cast<std::string>(kwargs["bitrate_mode"]);
             }
           })
      .def_rw("codec", &AudioEncoderConfig::codec)
      .def_rw("sample_rate", &AudioEncoderConfig::sample_rate)
      .def_rw("number_of_channels", &AudioEncoderConfig::number_of_channels)
      .def_rw("bitrate", &AudioEncoderConfig::bitrate)
      .def_rw("bitrate_mode", &AudioEncoderConfig::bitrate_mode);

  // AudioDecoderConfig クラス
  nb::class_<AudioDecoderConfig>(m, "AudioDecoderConfig")
      .def(nb::init<>())
      .def("__init__",
           [](AudioDecoderConfig* self, nb::kwargs kwargs) {
             new (self) AudioDecoderConfig();

             // 必須パラメータのチェック
             if (!kwargs.contains("codec")) {
               throw nb::value_error("codec is required");
             }
             if (!kwargs.contains("sample_rate")) {
               throw nb::value_error("sample_rate is required");
             }
             if (!kwargs.contains("number_of_channels")) {
               throw nb::value_error("number_of_channels is required");
             }

             // キーワード引数から値を設定
             self->codec = nb::cast<std::string>(kwargs["codec"]);
             self->sample_rate = nb::cast<uint32_t>(kwargs["sample_rate"]);
             self->number_of_channels =
                 nb::cast<uint32_t>(kwargs["number_of_channels"]);

             if (kwargs.contains("description")) {
               self->description = nb::cast<std::string>(kwargs["description"]);
             }
           })
      .def_rw("codec", &AudioDecoderConfig::codec)
      .def_rw("sample_rate", &AudioDecoderConfig::sample_rate)
      .def_rw("number_of_channels", &AudioDecoderConfig::number_of_channels)
      .def_rw("description", &AudioDecoderConfig::description);
  */

  // AudioDecoderSupport
  nb::class_<AudioDecoderSupport>(m, "AudioDecoderSupport")
      .def(nb::init<>())
      .def("__getitem__",
           [](const AudioDecoderSupport& self,
              const std::string& key) -> nb::object {
             if (key == "supported") {
               return nb::cast(self.supported);
             } else if (key == "config") {
               nb::dict d;
               d["codec"] = self.config.codec;
               d["sample_rate"] = self.config.sample_rate;
               d["number_of_channels"] = self.config.number_of_channels;
               if (self.config.description.has_value()) {
                 const auto& desc = self.config.description.value();
                 d["description"] = nb::bytes(
                     reinterpret_cast<const char*>(desc.data()), desc.size());
               }
               return nb::cast(d);
             } else {
               throw nb::key_error(("Unknown key: " + key).c_str());
             }
           });

  // VideoDecoderSupport
  nb::class_<VideoDecoderSupport>(m, "VideoDecoderSupport")
      .def(nb::init<>())
      .def("__getitem__",
           [](const VideoDecoderSupport& self,
              const std::string& key) -> nb::object {
             if (key == "supported") {
               return nb::cast(self.supported);
             } else if (key == "config") {
               nb::dict d;
               d["codec"] = self.config.codec;
               if (self.config.description.has_value()) {
                 const auto& desc = self.config.description.value();
                 d["description"] = nb::bytes(
                     reinterpret_cast<const char*>(desc.data()), desc.size());
               }
               if (self.config.coded_width.has_value())
                 d["coded_width"] = self.config.coded_width.value();
               if (self.config.coded_height.has_value())
                 d["coded_height"] = self.config.coded_height.value();
               if (self.config.display_aspect_width.has_value())
                 d["display_aspect_width"] =
                     self.config.display_aspect_width.value();
               if (self.config.display_aspect_height.has_value())
                 d["display_aspect_height"] =
                     self.config.display_aspect_height.value();
               d["hardware_acceleration_engine"] = self.config.hardware_acceleration_engine;
               if (self.config.optimize_for_latency.has_value())
                 d["optimize_for_latency"] =
                     self.config.optimize_for_latency.value();
               d["rotation"] = self.config.rotation;
               d["flip"] = self.config.flip;
               return nb::cast(d);
             } else {
               throw nb::key_error(("Unknown key: " + key).c_str());
             }
           });

  // AudioEncoderSupport
  nb::class_<AudioEncoderSupport>(m, "AudioEncoderSupport")
      .def(nb::init<>())
      .def("__getitem__",
           [](const AudioEncoderSupport& self,
              const std::string& key) -> nb::object {
             if (key == "supported") {
               return nb::cast(self.supported);
             } else if (key == "config") {
               nb::dict d;
               d["codec"] = self.config.codec;
               d["sample_rate"] = self.config.sample_rate;
               d["number_of_channels"] = self.config.number_of_channels;
               if (self.config.bitrate.has_value())
                 d["bitrate"] = self.config.bitrate.value();
               d["bitrate_mode"] = self.config.bitrate_mode;
               return nb::cast(d);
             } else {
               throw nb::key_error(("Unknown key: " + key).c_str());
             }
           });

  // VideoEncoderSupport
  nb::class_<VideoEncoderSupport>(m, "VideoEncoderSupport")
      .def(nb::init<>())
      .def("__getitem__",
           [](const VideoEncoderSupport& self,
              const std::string& key) -> nb::object {
             if (key == "supported") {
               return nb::cast(self.supported);
             } else if (key == "config") {
               nb::dict d;
               d["codec"] = self.config.codec;
               d["width"] = self.config.width;
               d["height"] = self.config.height;
               if (self.config.display_width.has_value())
                 d["display_width"] = self.config.display_width.value();
               if (self.config.display_height.has_value())
                 d["display_height"] = self.config.display_height.value();
               if (self.config.bitrate.has_value())
                 d["bitrate"] = self.config.bitrate.value();
               if (self.config.framerate.has_value())
                 d["framerate"] = self.config.framerate.value();
               d["hardware_acceleration"] = self.config.hardware_acceleration;
               d["alpha"] = self.config.alpha;
               if (self.config.scalability_mode.has_value())
                 d["scalability_mode"] = self.config.scalability_mode.value();
               d["bitrate_mode"] = self.config.bitrate_mode;
               d["latency_mode"] = self.config.latency_mode;
               if (self.config.content_hint.has_value())
                 d["content_hint"] = self.config.content_hint.value();
               d["hardware_acceleration_engine"] =
                   self.config.hardware_acceleration_engine;
               return nb::cast(d);
             } else {
               throw nb::key_error(("Unknown key: " + key).c_str());
             }
           });
}