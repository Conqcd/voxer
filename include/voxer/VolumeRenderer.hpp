#pragma once
#include "DataModel/Scene.hpp"
#include <voxer/Image.hpp>

class VoxerIRenderer;

namespace voxer {

class VolumeRenderer {
public:
  enum struct Type { OSPRay, OpenGL };

  explicit VolumeRenderer(Type type);
  ~VolumeRenderer();

  void set_camera(const Camera &);
  void add_volume(const Volume &volume);
  void add_isosurface(const Isosurface &isosurface);

  void render() const;

  auto get_colors() -> const Image &;

private:
  std::unique_ptr<VoxerIRenderer> impl;
};

} // namespace voxer