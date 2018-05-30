#include "DatasetManager.h"
#include "ospray/ospcommon/vec.h"
#include "third_party/RawReader/RawReader.h"
#include "third_party/rapidjson/document.h"
#include "third_party/rapidjson/istreamwrapper.h"
#include <utility>

using namespace std;
using vec3sz = ospcommon::vec_t<size_t, 3>;

OSPDataType typeForString(const std::string &dtype) {
  if (dtype == "uchar" || dtype == "char") {
    return OSP_UCHAR;
  }
  if (dtype == "float") {
    return OSP_FLOAT;
  }
  if (dtype == "double") {
    return OSP_DOUBLE;
  }

  return OSP_UCHAR;
}

void DatasetManager::load(string filepath) {
  rapidjson::Document d;
  ifstream filestream;
  filestream.open(filepath);
  if (!filestream.is_open()) {
    cerr << "Unable to open file datasets.json!" << endl;
    exit(1);
  }
  rapidjson::IStreamWrapper isw(filestream);
  d.ParseStream(isw);
  if (!d.IsArray()) {
    cerr << "Invalid data file!" << endl;
    exit(1);
  }

  vec3sz dimensions;
  for (auto &info : d.GetArray()) {
    if (!info.IsObject()) {
      cerr << "Invalid data file!" << endl;
      exit(1);
    }
    auto name = string(info["name"].GetString());
    auto filepath = string(info["path"].GetString());
    auto dimsData = info["dimensions"].GetArray();
    auto timesteps =
        info.HasMember("timesteps") ? info["timesteps"].GetInt() : 1;

    for (auto dim = dimsData.Begin(); dim != dimsData.end(); dim++) {
      dimensions[dim - dimsData.Begin()] = dim->GetInt();
    }
    auto dtype = string(info["dtype"].GetString());
    int sizeForType = 0;
    if (dtype == "uchar" || dtype == "char") {
      sizeForType = 1;
    } else if (dtype == "float") {
      sizeForType = 4;
    } else if (dtype == "double") {
      sizeForType = 8;
    }
    for (auto step = 1; step <= timesteps; step++) {
      auto _filepath = filepath;
      auto _name = name;
      if (timesteps > 1) {
        _filepath.replace(_filepath.end() - 10, _filepath.end() - 4,
                          to_string(step));
        _name += to_string(step);
      }
      gensv::RawReader reader(ospcommon::FileName(_filepath), dimensions,
                              sizeForType);
      Dataset dataset(ospcommon::vec3i(dimensions), dtype);
      auto size = reader.readRegion(vec3sz(0, 0, 0),
                                    dimensions, dataset.buffer.data());
      const OSPDataType ospVoxelType = typeForString(dtype);
      // const auto upper = ospcommon::vec3f(dimensions);
      // const auto halfLength = ospcommon::vec3i(dimensions) / 2;
      // auto volume =
      //     gensv::loadVolume(reader, ospcommon::vec3i(dimensions), dtype);
      // volume.bounds.lower -= ospcommon::vec3f(halfLength);
      // volume.bounds.upper -= ospcommon::vec3f(halfLength);
      // volume.volume.set("gridOrigin",
      //                   volume.ghostGridOrigin - ospcommon::vec3f(halfLength));
      // volume.volume.commit();
      datasets.emplace(_name, dataset);
      datasets[_name].data = new ospray::cpp::Data(datasets[_name].buffer.size(), ospVoxelType, datasets[_name].buffer.data(), OSP_DATA_SHARED_BUFFER);
    }
  }
  auto &a = datasets["lsabel1"];
    auto &b = datasets["lsabel8"];
    Dataset c(a.dimensions, a.dtype);
    for (auto i=0; i<a.buffer.size(); i++) {
      c.buffer[i] = (a.buffer[i] - b.buffer[i] + 255) / 2;
    }
    datasets.emplace("diff", c);
}

Dataset &DatasetManager::get(const char *name) {
  auto search = datasets.find(name);
  if (search == datasets.end()) {
    throw string("Volume ") + name + " not found";
  }
  return search->second;
}