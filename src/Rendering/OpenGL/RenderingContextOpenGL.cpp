#include "Rendering/OpenGL/RenderingContextOpenGL.hpp"
#include "Rendering/OpenGL/shaders.hpp"
#include <VMFoundation/pluginloader.h>
#include <VMGraphics/camera.h>
#include <VMUtils/cmdline.hpp>
#include <VMUtils/log.hpp>
#include <VMUtils/ref.hpp>
#include <VMat/geometry.h>
#include <stdexcept>

using namespace vm;
using namespace std;

namespace {

Bound3f bound({-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f});
Point3f CubeVertices[8];
Point3f CubeTexCoords[8];

unsigned int CubeVertexIndices[] = {0, 2, 1, 1, 2, 3, 4, 5, 6, 5, 7, 6,
                                    0, 1, 4, 1, 5, 4, 2, 6, 3, 3, 6, 7,
                                    0, 4, 2, 2, 4, 6, 1, 3, 5, 3, 7, 5};

GL::GLShader glCall_CreateShaderAndCompileHelper(GL &gl, GLenum shaderType,
                                                 const char *source) {
  auto handle = gl.CreateShader(shaderType);
  GL_EXPR(glShaderSource(handle, 1, &source, nullptr));
  GL_EXPR(glCompileShader(handle));
  int success = 1;
  char infoLog[1024];
  GL_EXPR(glGetShaderiv(handle, GL_COMPILE_STATUS, &success));
  if (!success) {
    GL_EXPR(glGetShaderInfoLog(handle, 1024, nullptr, infoLog));
    println("ERROR::SHADER::COMPILATION_FAILED, {}", infoLog);
    exit(-1);
  }
  return handle;
}

void glCall_LinkProgramAndCheckHelper(GL::GLProgram &program) {
  // link
  GL_EXPR(glLinkProgram(program));
  int success;
  char infoLog[1024];
  GL_EXPR(glGetProgramiv(program, GL_LINK_STATUS, &success));

  if (!success) {
    glGetProgramInfoLog(program, 1024, nullptr, infoLog);
    println("ERROR::SHADER::PROGRAM::LINKING_FAILED:{}", infoLog);
    exit(-1);
  }
}

void glCall_CameraUniformUpdate(ViewingTransform &camera,
                                Transform &modelMatrix,
                                GL::GLProgram &positionGenerateProgram) {
  // camera
  const auto mvpTransform =
      camera.GetPerspectiveMatrix() * camera.GetViewMatrixWrapper().LookAt();
  const auto viewTransform = camera.GetViewMatrixWrapper().LookAt();
  const auto viewPos = camera.GetViewMatrixWrapper().GetPosition();
  assert(positionGenerateProgram.Valid());
  GL_EXPR(glProgramUniformMatrix4fv(
      positionGenerateProgram, 0, 1, GL_TRUE,
      mvpTransform.Matrix().FlatData())); // location = 0 is MVPMatrix
  GL_EXPR(glProgramUniformMatrix4fv(
      positionGenerateProgram, 1, 1, GL_TRUE,
      modelMatrix.Matrix().FlatData())); // location = 1 is ModelMatrix
  GL_EXPR(glProgramUniform3fv(positionGenerateProgram, 2, 1,
                              viewPos.ConstData())); // location = 1 is viewPos
}

} // namespace

namespace voxer {

RenderingContextOpenGL::RenderingContextOpenGL() : width(400), height(400) {
  if (!glfwInit()) {
    throw runtime_error("create window failed");
  }

  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
  glfwWindowHint(GLFW_DOUBLEBUFFER, GL_FALSE);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  window = glfwCreateWindow(width, height, "voxer", nullptr, nullptr);
  glfwHideWindow(window);

  glfwMakeContextCurrent(window);

  if (!gladLoadGL()) {
    throw runtime_error("failed to load gl");
  }

  for (int i = 0; i < 8; i++) {
    CubeVertices[i] = bound.Corner(i);
    CubeTexCoords[i] = bound.Corner(i);
  }

  gl = GL::NEW();

  // Prepare data:
  // [1] Initilize vertex buffer
  vao = gl->CreateVertexArray();
  GL_EXPR(glBindVertexArray(vao));
  vbo = gl->CreateBuffer();
  GL_EXPR(glNamedBufferStorage(vbo, sizeof(CubeVertices), CubeVertices,
                               GL_DYNAMIC_STORAGE_BIT));
  GL_EXPR(glVertexArrayVertexBuffer(vao, 0, vbo, 0, sizeof(Point3f)));

  ebo = gl->CreateBuffer();
  GL_EXPR(glNamedBufferStorage(ebo, sizeof(CubeVertexIndices),
                               CubeVertexIndices, GL_DYNAMIC_STORAGE_BIT));
  GL_EXPR(glVertexArrayElementBuffer(vao, ebo));

  // layout(location = 0)
  GL_EXPR(glEnableVertexArrayAttrib(vao, 0));
  // layout(location = 1)
  GL_EXPR(glEnableVertexArrayAttrib(vao, 1));

  GL_EXPR(glVertexArrayAttribFormat(vao, 0, 3, GL_FLOAT, GL_FALSE, 0));
  GL_EXPR(glVertexArrayAttribFormat(vao, 1, 3, GL_FLOAT, GL_FALSE, 0));

  GL_EXPR(glVertexArrayAttribBinding(vao, 0, 0));
  GL_EXPR(glVertexArrayAttribBinding(vao, 1, 0));

  GL_EXPR(glNamedBufferSubData(vbo, 0, sizeof(CubeVertices), CubeVertices));
  GL_EXPR(glNamedBufferSubData(ebo, 0, sizeof(CubeVertexIndices),
                               CubeVertexIndices));

  /*Create shader program:*/
  //[1] bounding box vertex shader
  auto vShader = glCall_CreateShaderAndCompileHelper(*gl, GL_VERTEX_SHADER,
                                                     shader::position_v);
  auto fShader = glCall_CreateShaderAndCompileHelper(*gl, GL_FRAGMENT_SHADER,
                                                     shader::position_f);
  positionGenerateProgram = gl->CreateProgram();
  GL_EXPR(glAttachShader(positionGenerateProgram, vShader));
  GL_EXPR(glAttachShader(positionGenerateProgram, fShader));
  // Set Fragment output location, the processure must be done before linking
  // the program
  GL_EXPR(glBindFragDataLocation(positionGenerateProgram, 0, "entryPos"));
  GL_EXPR(glBindFragDataLocation(positionGenerateProgram, 1, "exitPos"));
  glCall_LinkProgramAndCheckHelper(positionGenerateProgram);

  //[2] ray casting shader
  vShader = glCall_CreateShaderAndCompileHelper(*gl, GL_VERTEX_SHADER,
                                                shader::screenquad_v);
  fShader = glCall_CreateShaderAndCompileHelper(*gl, GL_FRAGMENT_SHADER,
                                                shader::naiveraycast_f);
  raycastingProgram = gl->CreateProgram();
  GL_EXPR(glAttachShader(raycastingProgram, vShader));
  GL_EXPR(glAttachShader(raycastingProgram, fShader));
  glCall_LinkProgramAndCheckHelper(raycastingProgram);

  //[3] screen rendering program (render the result texture onto the screen (Do
  // not use Blit api))
  fShader = glCall_CreateShaderAndCompileHelper(*gl, GL_FRAGMENT_SHADER,
                                                shader::screenquad_f);
  screenQuadProgram = gl->CreateProgram();
  GL_EXPR(glAttachShader(screenQuadProgram, vShader));
  GL_EXPR(glAttachShader(screenQuadProgram, fShader));
  glCall_LinkProgramAndCheckHelper(screenQuadProgram);

  // Shaders could be deleted after linked
  gl->DeleteGLObject(vShader);
  gl->DeleteGLObject(fShader);

  //[2] ray-casting shader
  GL::GLSampler sampler = gl->CreateSampler();
  GL_EXPR(glSamplerParameterf(sampler, GL_TEXTURE_MIN_FILTER,
                              GL_LINEAR)); // filter type
  GL_EXPR(glSamplerParameterf(sampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
  GL_EXPR(glSamplerParameterf(sampler, GL_TEXTURE_WRAP_R,
                              GL_CLAMP_TO_EDGE)); // boarder style
  GL_EXPR(glSamplerParameterf(sampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
  GL_EXPR(glSamplerParameterf(sampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));

  GL_EXPR(glProgramUniform1i(raycastingProgram, 3, 0));
  // sets location = 0 (tf texture sampler) as tf texture unit 0
  GL_EXPR(glBindSampler(0, sampler));
  // sets location = 1 (volume texture sampler)
  GL_EXPR(glProgramUniform1i(raycastingProgram, 4, 1));
  // as volume texture unit 1
  GL_EXPR(glBindSampler(1, sampler));

  // sets location = 2 as entry image unit 0
  GL_EXPR(glProgramUniform1i(raycastingProgram, 0, 0));
  // sets location = 3 as exit image unit 1
  GL_EXPR(glProgramUniform1i(raycastingProgram, 1, 1));
  // sets location = 4 as result image unit 2
  GL_EXPR(glProgramUniform1i(raycastingProgram, 2, 2));

  //[3] screen rendering shader
  // sets location = 0 as result image unit 2
  GL_EXPR(glProgramUniform1i(screenQuadProgram, 0, 2));

  glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
}

RenderingContextOpenGL::~RenderingContextOpenGL() {
  glfwDestroyWindow(window);
  glfwTerminate();
}

void RenderingContextOpenGL::render(const Scene &scene,
                                    DatasetStore &datasets) {
  /*Model Matrix*/
  Transform ModelTransform;
  ModelTransform.SetIdentity();

  auto &camera = scene.camera;
  width = camera.width;
  height = camera.height;

  ViewingTransform vm_camera{{camera.pos[0], camera.pos[1], camera.pos[2]},
                             {camera.up[0], camera.up[1], camera.up[2]},
                             {0, 0, 0}};

  //[2] Create transfer function texture
  auto GLTFTexture = gl->CreateTexture(GL_TEXTURE_1D);
  assert(GLTFTexture.Valid());
  GL_EXPR(glTextureStorage1D(GLTFTexture, 1, GL_RGBA32F, 256));
  auto interploted = interpolate_tfcn(scene.tfcns[0]);
  vector<array<float, 4>> tfcn_data(interploted.first.size());
  // TODO: inefficient
  for (size_t i = 0; i < tfcn_data.size(); i++) {
    auto opacity = interploted.first[i];
    auto &color = interploted.second[i];
    tfcn_data[i] = {color[0], color[1], color[2], opacity};
  }
  GL_EXPR(glTextureSubImage1D(GLTFTexture, 0, 0, interploted.first.size(),
                              GL_RGBA, GL_FLOAT, tfcn_data.data()));

  // Create render targets
  auto GLFramebuffer = gl->CreateFramebuffer();
  auto GLEntryPosTexture = gl->CreateTexture(GL_TEXTURE_2D);
  assert(GLEntryPosTexture.Valid());
  auto GLExitPosTexture = gl->CreateTexture(GL_TEXTURE_2D);
  assert(GLExitPosTexture.Valid());
  auto GLResultTexture = gl->CreateTexture(GL_TEXTURE_2D);
  assert(GLResultTexture.Valid());

  GL_EXPR(glTextureStorage2D(GLEntryPosTexture, 1, GL_RGBA32F, camera.width,
                             camera.height));
  GL_EXPR(glTextureStorage2D(GLExitPosTexture, 1, GL_RGBA32F, camera.width,
                             camera.height));
  GL_EXPR(glTextureStorage2D(GLResultTexture, 1, GL_RGBA32F, camera.width,
                             camera.height));

  GL_EXPR(glNamedFramebufferTexture(GLFramebuffer, GL_COLOR_ATTACHMENT0,
                                    GLEntryPosTexture, 0));
  GL_EXPR(glNamedFramebufferTexture(GLFramebuffer, GL_COLOR_ATTACHMENT1,
                                    GLExitPosTexture, 0));
  GL_EXPR(glNamedFramebufferTexture(GLFramebuffer, GL_COLOR_ATTACHMENT2,
                                    GLResultTexture, 0));
  // Depth and stencil attachments are not necessary in Ray Casting.

  GL_EXPR(if (glCheckNamedFramebufferStatus(GLFramebuffer, GL_FRAMEBUFFER) !=
              GL_FRAMEBUFFER_COMPLETE) {
    cout << "Framebuffer object is not complete." << endl;
    exit(-1);
  });

  // Create Volume Texture
  auto &scene_dataset = scene.datasets[0];
  auto &dataset = datasets.get_or_create(scene_dataset, scene.datasets);
  auto &dimensions = dataset.info.dimensions;

  // Create Volume Texture
  auto volume_tex = gl->CreateTexture(GL_TEXTURE_3D);
  assert(volume_tex.Valid());
  GL_EXPR(glBindTextureUnit(1, volume_tex));
  GL_EXPR(glTextureStorage3D(volume_tex, 1, GL_R8, dimensions[0], dimensions[1],
                             dimensions[2]));
  GL_EXPR(glTextureSubImage3D(volume_tex, 0, 0, 0, 0, dimensions[0],
                              dimensions[1], dimensions[2], GL_RED,
                              GL_UNSIGNED_BYTE, dataset.buffer.data()));
  GL_EXPR(glBindTextureUnit(1, volume_tex));

  /* Texture unit and image unit binding*/
  // [1] binds texture unit : see the raycasting shader for details
  // binds texture unit 0 for tf texture
  GL_EXPR(glBindTextureUnit(0, GLTFTexture));
  // [2] binds image unit : see the raycasting shader for details
  // binds image unit 0 for entry texture (read and write)
  GL_EXPR(glBindImageTexture(0, GLEntryPosTexture, 0, GL_FALSE, 0,
                             GL_READ_WRITE, GL_RGBA32F));
  // binds image unit 1 for exit texture (read and write)
  GL_EXPR(glBindImageTexture(1, GLExitPosTexture, 0, GL_FALSE, 0, GL_READ_WRITE,
                             GL_RGBA32F));
  // binds image unit 2 for result texture (read and write)
  GL_EXPR(glBindImageTexture(2, GLResultTexture, 0, GL_FALSE, 0, GL_READ_WRITE,
                             GL_RGBA32F));

  /* Uniforms binding for program*/
  //[1] position shader
  // camera-related uniforms for position program
  glCall_CameraUniformUpdate(vm_camera, ModelTransform,
                             positionGenerateProgram);

  /*Configuration rendering state*/
  const float zeroRGBA[] = {0.f, 0.f, 0.f, 0.f};
  // Just add dst to src : (srcRBG * 1 + dstRGB * 1,srcAlpha * 1 +
  // dstRGB * 1), so the backround color must be cleared as 0
  GL_EXPR(glBlendFuncSeparate(GL_ONE, GL_ONE, GL_ONE, GL_ONE));
  GL_EXPR(glFrontFace(GL_CW));

  const GLenum drawBuffers[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
  const GLenum allDrawBuffers[3] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1,
                                    GL_COLOR_ATTACHMENT2};

  glViewport(0, 0, camera.width, camera.height);
  glClear(GL_COLOR_BUFFER_BIT);

  glEnable(GL_BLEND); // Blend is necessary for ray-casting position generation
  GL_EXPR(glUseProgram(positionGenerateProgram));
  GL_EXPR(glBindFramebuffer(GL_FRAMEBUFFER, GLFramebuffer));

  /**
   * @brief Clearing framebuffer by using the non-DSA api because commented DSA
   * version above has no effect on intel GPU. Maybe it's a graphics driver bug.
   * see
   * https://software.intel.com/en-us/forums/graphics-driver-bug-reporting/topic/740117
   */
  GL_EXPR(glDrawBuffers(3, allDrawBuffers));
  GL_EXPR(glClearBufferfv(GL_COLOR, 0, zeroRGBA)); // Clear EntryPosTexture
  GL_EXPR(glClearBufferfv(GL_COLOR, 1, zeroRGBA)); // CLear ExitPosTexture
  GL_EXPR(glClearBufferfv(GL_COLOR, 2, zeroRGBA)); // Clear ResultTexture

  // draw into these buffers
  GL_EXPR(glNamedFramebufferDrawBuffers(GLFramebuffer, 2, drawBuffers));
  // 12 triangles, 36 vertices in total
  GL_EXPR(glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr));

  // Pass [2 - n]: Ray casting here
  GL_EXPR(glDisable(GL_BLEND));

  GL_EXPR(glUseProgram(raycastingProgram));
  // draw into result texture
  GL_EXPR(glNamedFramebufferDrawBuffer(GLFramebuffer, GL_COLOR_ATTACHMENT2));
  // vertex is hard coded in shader
  GL_EXPR(glDrawArrays(GL_TRIANGLE_STRIP, 0, 4));

  // Pass [n + 1]: Blit result to default framebuffer
  // prepare to display
  GL_EXPR(glBindFramebuffer(GL_FRAMEBUFFER, 0));

  GL_EXPR(glUseProgram(screenQuadProgram));
  // vertex is hard coded in shader
  GL_EXPR(glDrawArrays(GL_TRIANGLE_STRIP, 0, 4));

  glFlush();
}

auto RenderingContextOpenGL::get_colors() -> const Image & {
  image.width = width;
  image.height = width;
  image.channels = 3;
  image.data.resize(width * height * 3);

  glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE,
               reinterpret_cast<void *>(image.data.data()));

  return image;
}

} // namespace voxer