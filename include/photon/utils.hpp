#ifndef PHOTON_UTILS
#define PHOTON_UTILS

#include "horizon/core/model.hpp"
#include "photon/types.hpp"

namespace photon {

model_t raw_model_to_model(core::ref<gfx::base_t> base,
                           const std::filesystem::path &photon_assets_path,
                           const core::raw_model_t &raw_model);

} // namespace photon

#endif // !PHOTON_UTILS
