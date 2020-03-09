// SPDX-License-Identifier: MIT
#include <future>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "Jsonnet.hpp"
#include "JsonnetWorker.hpp"
#include "JsonValueConverter.hpp"

namespace nodejsonnet {

  Napi::FunctionReference Jsonnet::constructor;

  Napi::Object Jsonnet::init(Napi::Env env) {
    auto const func = DefineClass(env, "Jsonnet", {
        StaticAccessor("version", &Jsonnet::getVersion, nullptr),
        InstanceMethod("setMaxStack", &Jsonnet::setMaxStack),
        InstanceMethod("setMaxTrace", &Jsonnet::setMaxTrace),
        InstanceMethod("setGcMinObjects", &Jsonnet::setGcMinObjects),
        InstanceMethod("setGcGrowthTrigger", &Jsonnet::setGcGrowthTrigger),
        InstanceMethod("evaluateFile", &Jsonnet::evaluateFile),
        InstanceMethod("evaluateSnippet", &Jsonnet::evaluateSnippet),
        InstanceMethod("extString", &Jsonnet::extString),
        InstanceMethod("extCode", &Jsonnet::extCode),
        InstanceMethod("tlaString", &Jsonnet::tlaString),
        InstanceMethod("tlaCode", &Jsonnet::tlaCode),
        InstanceMethod("addJpath", &Jsonnet::addJpath),
        InstanceMethod("nativeCallback", &Jsonnet::nativeCallback),
      });

    constructor = Napi::Persistent(func);
    constructor.SuppressDestruct();
    return func;
  }

  Jsonnet::Jsonnet(const Napi::CallbackInfo &info)
    : Napi::ObjectWrap<Jsonnet>(info) {
  }

  Napi::Value Jsonnet::getVersion(const Napi::CallbackInfo& info) {
    auto const env = info.Env();
    return Napi::String::New(env, ::jsonnet_version());
  }

  Napi::Value Jsonnet::setMaxStack(const Napi::CallbackInfo &info) {
    maxStack = info[0].As<Napi::Number>();
    return info.This();
  }

  Napi::Value Jsonnet::setMaxTrace(const Napi::CallbackInfo &info) {
    maxTrace = info[0].As<Napi::Number>();
    return info.This();
  }

  Napi::Value Jsonnet::setGcMinObjects(const Napi::CallbackInfo &info) {
    gcMinObjects = info[0].As<Napi::Number>();
    return info.This();
  }

  Napi::Value Jsonnet::setGcGrowthTrigger(const Napi::CallbackInfo &info) {
    gcGrowthTrigger = info[0].As<Napi::Number>();
    return info.This();
  }

  Napi::Value Jsonnet::evaluateFile(const Napi::CallbackInfo& info) {
    auto const env = info.Env();
    auto filename = info[0].As<Napi::String>().Utf8Value();

    auto vm = createVm(env);
    auto const worker = new JsonnetWorker(env, vm, std::make_unique<JsonnetWorker::EvaluateFileOp>(std::move(filename)));
    auto const promise = worker->Promise();
    worker->Queue();
    return promise;
  }

  Napi::Value Jsonnet::evaluateSnippet(const Napi::CallbackInfo& info) {
    auto const env = info.Env();
    auto snippet = info[0].As<Napi::String>().Utf8Value();
    auto filename = info.Length() < 2 ? "(snippet)" : info[1].As<Napi::String>().Utf8Value();

    auto vm = createVm(env);
    auto const worker = new JsonnetWorker(env, vm, std::make_unique<JsonnetWorker::EvaluateSnippetOp>(std::move(snippet), std::move(filename)));
    auto const promise = worker->Promise();
    worker->Queue();
    return promise;
  }

  Napi::Value Jsonnet::extString(const Napi::CallbackInfo& info) {
    ext.insert_or_assign(
      info[0].As<Napi::String>().Utf8Value(),
      Variable{false, info[1].As<Napi::String>().Utf8Value()});

    return info.This();
  }

  Napi::Value Jsonnet::extCode(const Napi::CallbackInfo& info) {
    ext.insert_or_assign(
      info[0].As<Napi::String>().Utf8Value(),
      Variable{true, info[1].As<Napi::String>().Utf8Value()});

    return info.This();
  }

  Napi::Value Jsonnet::tlaString(const Napi::CallbackInfo& info) {
    tla.insert_or_assign(
      info[0].As<Napi::String>().Utf8Value(),
      Variable{false, info[1].As<Napi::String>().Utf8Value()});

    return info.This();
  }

  Napi::Value Jsonnet::tlaCode(const Napi::CallbackInfo& info) {
    tla.insert_or_assign(
      info[0].As<Napi::String>().Utf8Value(),
      Variable{true, info[1].As<Napi::String>().Utf8Value()});

    return info.This();
  }

  Napi::Value Jsonnet::addJpath(const Napi::CallbackInfo& info) {
    jpath.push_back(info[0].As<Napi::String>().Utf8Value());

    return info.This();
  }

  namespace {

    struct TsfnWrap {
      TsfnWrap(Napi::ThreadSafeFunction tsfn): tsfn{tsfn} {
      }

      ~TsfnWrap() {
        if(tsfn) {
          tsfn.Release();
        }
      }

      TsfnWrap(TsfnWrap const &other) {
        *this = other;
      }

      TsfnWrap(TsfnWrap &&other) {
        *this = std::move(other);
      }

      TsfnWrap &operator=(TsfnWrap const &other) {
        tsfn = other.tsfn;
        if(tsfn) {
          tsfn.Acquire();
        }
        return *this;
      }

      TsfnWrap &operator=(TsfnWrap &&other) {
        if(tsfn) {
          tsfn.Release();
        }
        tsfn = other.tsfn;
        other.tsfn = {};
        return *this;
      }

      Napi::ThreadSafeFunction const *operator->() const {
        return &tsfn;
      }

    private:
      Napi::ThreadSafeFunction tsfn;
    };

  }

  Napi::Value Jsonnet::nativeCallback(const Napi::CallbackInfo &info) {
    auto name = info[0].As<Napi::String>().Utf8Value();
    auto const fun = info[1].As<Napi::Function>();

    std::vector<std::string> params;
    for(size_t i = 2; i < info.Length(); ++i) {
      params.push_back(info[i].As<Napi::String>().Utf8Value());
    }

    nativeCallbacks.insert_or_assign(
      std::move(name),
      NativeCallback{std::make_shared<Napi::FunctionReference>(Napi::Persistent(fun)), std::move(params)});

    return info.This();
  }

  std::shared_ptr<JsonnetVm> Jsonnet::createVm(Napi::Env const &env) {
    auto vm = JsonnetVm::make();

    if(maxStack) {
      vm->maxStack(*maxStack);
    }
    if(maxTrace) {
      vm->maxTrace(*maxTrace);
    }
    if(gcMinObjects) {
      vm->gcMinObjects(*gcMinObjects);
    }
    if(gcGrowthTrigger) {
      vm->gcGrowthTrigger(*gcGrowthTrigger);
    }

    for(auto const &[name, var]: ext) {
      if(var.isCode) {
        vm->extCode(name, var.value);
      } else {
        vm->extVar(name, var.value);
      }
    }

    for(auto const &[name, var]: tla) {
      if(var.isCode) {
        vm->tlaCode(name, var.value);
      } else {
        vm->tlaVar(name, var.value);
      }
    }

    for(auto const &x: jpath) {
      vm->jpathAdd(x);
    }

    struct Payload {
      Payload(std::shared_ptr<JsonnetVm> vm, std::vector<JsonnetJsonValue const *> args)
        : args{std::move(args)}, vm{std::move(vm)} {
      }

      std::vector<JsonnetJsonValue const *> const &getArgs() const {
        return args;
      }

      std::shared_ptr<JsonnetVm> getVm() const {
        return vm;
      }

      void setResult(JsonnetJsonValue *value) {
        result.set_value(value);
      }

      void setError(std::exception_ptr e) {
        result.set_exception(e);
      }

      std::future<JsonnetJsonValue *> getFuture() {
        return result.get_future();
      }

    private:
      std::vector<JsonnetJsonValue const *> args;
      std::shared_ptr<JsonnetVm> vm;
      std::promise<JsonnetJsonValue *> result;
    };

    for(auto const &[name, cb]: nativeCallbacks) {
      auto const &fun = cb.fun;
      auto const &params = cb.params;

      TsfnWrap tsfn = Napi::ThreadSafeFunction::New(env, fun->Value(), "Jsonnet Native Callback", 0, 1);

      auto callback =
        [](Napi::Env env, Napi::Function fun, Payload *payload) {
          // This functions runs in the Node main thread.

          JsonValueConverter const conv{payload->getVm()};

          std::vector<napi_value> args;
          args.reserve(payload->getArgs().size());
          for(auto const arg: payload->getArgs()) {
            args.push_back(conv.toNapiValue(env, arg));
          }

          auto const result = fun.Call(args);
          if(!result.IsPromise()) {
            payload->setResult(conv.toJsonnetJson(result));
            return;
          }

          auto const on_success = Napi::Function::New(
            env,
            [](Napi::CallbackInfo const &info){
              auto const payload = static_cast<Payload *>(info.Data());
              JsonValueConverter const conv{payload->getVm()};
              payload->setResult(conv.toJsonnetJson(info[0]));
            },
            "onSuccess",
            payload
            );

          auto const on_failure = Napi::Function::New(
            env,
            [](Napi::CallbackInfo const &info){
              auto const payload = static_cast<Payload *>(info.Data());
              auto const error = info[0].ToString();
              payload->setError(std::make_exception_ptr(std::runtime_error(error)));
            },
            "onFailure",
            payload
            );

          auto const promise = result.As<Napi::Object>();
          promise.Get("then").As<Napi::Function>().Call(promise, {on_success, on_failure});
        };

      auto const native_callback =
        [=, tsfn = std::move(tsfn)](std::shared_ptr<JsonnetVm> vm, std::vector<JsonnetJsonValue const *> args) {
          // This functions runs in a worker thread and cannot access Node VM.

          Payload payload(std::move(vm), std::move(args));
          tsfn->BlockingCall(&payload, callback);
          return payload.getFuture().get();
        };

      vm->nativeCallback(name, std::move(native_callback), params);
    }

    return vm;
  }
}
