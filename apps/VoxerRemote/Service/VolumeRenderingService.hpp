#pragma once
#include "DataModel/Scene.hpp"
#include "Service/AbstractService.hpp"
#include "Store/DatasetStore.hpp"
#include "utils.hpp"
#include <future>
#include <memory>
#include <voxer/Rendering/VolumeRenderer.hpp>

namespace voxer::remote {

class VolumeRenderingService final : public AbstractService {
public:
  VolumeRenderingService();

  void on_message(const char *message, uint32_t size) final;

  [[nodiscard]] auto get_path() const -> std::string final {
    return "/render";
  }

  voxer::remote::DatasetStore *m_datasets = nullptr;

private:
  std::unique_ptr<voxer::VolumeRenderer> m_opengl;
  std::unique_ptr<voxer::VolumeRenderer> m_ospray;

  void traverse_scene(VolumeRenderer &renderer, const Scene &scene) const;

  auto parse(const char *message, uint32_t size)
      -> std::pair<VolumeRenderer::Type, Scene>;
};

} // namespace voxer::remote
