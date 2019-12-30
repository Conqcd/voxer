#pragma once
#include <string>
#include <vector>
#include <voxer/scene/Camera.hpp>
#include <voxer/scene/Isosurface.hpp>
#include <voxer/scene/Light.hpp>
#include <voxer/scene/SceneDataset.hpp>
#include <voxer/scene/Slice.hpp>
#include <voxer/scene/TransferFunction.hpp>
#include <voxer/scene/Volume.hpp>

namespace voxer {

struct Scene {
  std::vector<SceneDataset> datasets;
  std::vector<TransferFunction> tfcns;
  std::vector<Volume> volumes;
  std::vector<Isosurface> isosurfaces;
  std::vector<Slice> slices;
  std::vector<Light> lights;
  Camera camera;

  auto serialize() -> std::string;
  static auto deserialize(simdjson::ParsedJson::Iterator &pjh) -> Scene;
};

} // namespace voxer

namespace formatter {

template <> inline auto registerMembers<voxer::Scene>() {
  using Scene = voxer::Scene;
  return std::make_tuple(
      member("datasets", &Scene::datasets), member("volumes", &Scene::volumes),
      member("tfcns", &Scene::tfcns),
      member("isosurfaces", &Scene::isosurfaces),
      member("slices", &Scene::slices), member("camera", &Scene::camera));
}

} // namespace formatter