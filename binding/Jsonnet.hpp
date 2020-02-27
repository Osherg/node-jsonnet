// SPDX-License-Identifier: MIT
#pragma once

#include <napi.h>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "JsonnetVm.hpp"

namespace nodejsonnet {

  struct SharedFunRef {
    SharedFunRef(Napi::FunctionReference &&ref)
      : ref{new Napi::FunctionReference(std::move(ref))} {
    }

    SharedFunRef(SharedFunRef const &other) {
      *this = other;
    }

    SharedFunRef(SharedFunRef &&other) {
      *this = std::move(other);
    }

    SharedFunRef &operator=(SharedFunRef const &other) {
      ref = other.ref;
      return *this;
    }

    SharedFunRef &operator=(SharedFunRef &&other) {
      ref = std::move(other.ref);
      return *this;
    }

    Napi::Function operator*() const {
      return ref->Value();
    }

  private:
    std::shared_ptr<Napi::FunctionReference> ref;
  };

  struct JsonnetVmParam {
    struct VariableAssignment {
      bool isCode;
      std::string key;
      std::string value;
    };

    struct NativeCallback {
      std::string name;
      SharedFunRef fun;
      std::vector<std::string> params;
    };

    std::vector<VariableAssignment> ext, tla;
    std::vector<std::string> jpath;
    std::vector<NativeCallback> nativeCallbacks;
  };

  class Jsonnet: public Napi::ObjectWrap<Jsonnet>, private JsonnetVmParam {
  public:
    static Napi::Object init(const Napi::Env env);

    explicit Jsonnet(const Napi::CallbackInfo &info);

  private:
    static Napi::FunctionReference constructor;
    static Napi::Value getVersion(const Napi::CallbackInfo &info);

    Napi::Value evaluateFile(const Napi::CallbackInfo &info);
    Napi::Value evaluateSnippet(const Napi::CallbackInfo &info);
    Napi::Value extString(const Napi::CallbackInfo &info);
    Napi::Value extCode(const Napi::CallbackInfo &info);
    Napi::Value tlaString(const Napi::CallbackInfo &info);
    Napi::Value tlaCode(const Napi::CallbackInfo &info);
    Napi::Value addJpath(const Napi::CallbackInfo &info);
    Napi::Value nativeCallback(const Napi::CallbackInfo &info);

    std::shared_ptr<JsonnetVm> createVm(Napi::Env const &env);
  };

}
