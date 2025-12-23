#include "public.sdk/source/main/pluginfactory.h"
#include "plugin_processor.hpp"
#include "plugin_controller.hpp"
#include "plugin_ids.hpp"
#include "version.h"

#define stringPluginName "Spectrum EQ"

BEGIN_FACTORY_DEF("SpectrumEQ",
                  "https://github.com/spectrum-eq",
                  "mailto:info@spectrum-eq.com")

    // Processor
    DEF_CLASS2(INLINE_UID_FROM_FUID(SpectrumEQ::kProcessorUID),
               PClassInfo::kManyInstances,
               kVstAudioEffectClass,
               stringPluginName,
               Vst::kDistributable,
               Vst::PlugType::kFxEQ,
               FULL_VERSION_STR,
               kVstVersionString,
               SpectrumEQ::PluginProcessor::createInstance)

    // Controller
    DEF_CLASS2(INLINE_UID_FROM_FUID(SpectrumEQ::kControllerUID),
               PClassInfo::kManyInstances,
               kVstComponentControllerClass,
               stringPluginName "Controller",
               0,
               "",
               FULL_VERSION_STR,
               kVstVersionString,
               SpectrumEQ::PluginController::createInstance)

END_FACTORY

