#ifndef IMAGE_HPP
#define IMAGE_HPP

#include "horizon/core/core.hpp"
#include "horizon/core/math.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>

class image_t {
public:
  image_t(uint64_t width, uint64_t height) {
    this->width = width;
    this->height = height;
    _p_pixels = new core::vec4[width * height];
  }

  ~image_t() { delete[] _p_pixels; }

  core::vec4 &at(uint64_t x, uint64_t y) { return _p_pixels[y * width + x]; }
  const core::vec4 &at(uint64_t x, uint64_t y) const {
    return _p_pixels[y * width + x];
  }

  uint64_t width, height;

private:
  friend void save_image_to_disk(const image_t &,
                                 const std::filesystem::path &);

  core::vec4 *_p_pixels;
};

inline void save_image_to_disk(const image_t &image,
                               const std::filesystem::path &path) {
  std::stringstream ss{};
  ss << "P3\n" << image.width << ' ' << image.height << "\n255\n";
  for (int64_t j = image.height - 1; j >= 0; j--) {
    for (int64_t i = 0; i < image.width; i++) {
      core::vec4 pixel = image.at(i, j);
      ss << uint32_t(core::clamp(pixel.r, 0, 1) * 255) << ' ';
      ss << uint32_t(core::clamp(pixel.g, 0, 1) * 255) << ' ';
      ss << uint32_t(core::clamp(pixel.b, 0, 1) * 255) << '\n';
    }
  }
  std::ofstream file{path};
  if (!file.is_open())
    throw std::runtime_error("Failed to open file!");
  file << ss.str();
  file.close();
}

#endif
