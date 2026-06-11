#pragma once

#include "Interface/IExecutor.h"
#include "Interface/IContext.h"
#include "../../Core/Include/RuntimeState.h"

#define ZERI_PLUGIN_ABI_VERSION 1

extern "C" {
    typedef const char* (*zeri_plugin_name_fn)();
    typedef const char* (*zeri_plugin_version_fn)();
    typedef int (*zeri_plugin_abi_version_fn)();
    typedef Zeri::Engines::IExecutor* (*zeri_create_executor_fn)();
    typedef Zeri::Engines::IContext* (*zeri_create_context_fn)(Zeri::Core::RuntimeState&);
    typedef void (*zeri_destroy_executor_fn)(Zeri::Engines::IExecutor*);
    typedef void (*zeri_destroy_context_fn)(Zeri::Engines::IContext*);
}

/*
ZeriPluginABI.h
Defines the stable C ABI contract for native Zeri plugins.
The ABI version constant and exported function typedefs are frozen at v1.
*/
