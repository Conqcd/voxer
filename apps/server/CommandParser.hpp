#pragma once
#include <functional>
#include <iostream>
#include <simdjson/jsonparser.h>
#include <string>
#include <utility>
#include <variant>
#include <voxer/Scene.hpp>

using SceneModifier = std::function<voxer::Scene(const voxer::Scene &)>;

struct Command {
  enum class Type { Render, Save, Query, RunPipeline };

  Type type = Type::Render;
  std::variant<voxer::Scene,                          // for render
               std::pair<std::string, voxer::Scene>,  // for save
               std::pair<std::string, SceneModifier>, // for run
               std::nullptr_t                         // for query
               >
      params = nullptr;
};

class CommandParser {
public:
  auto parse(const std::string &value) -> Command;
  auto parse(const char *value, uint64_t size) -> Command;

private:
  simdjson::ParsedJson pj;
};