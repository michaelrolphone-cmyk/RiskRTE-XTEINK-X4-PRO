#pragma once
#include <RiscDiagnosticSourceV1.h>
// Graph-only host fixture. Production main binds the optional native source;
// these tests exercise hardware materialization with no diagnostic records.
inline bool bind_empty_diagnostic_source(RiscBoot::Runtime& runtime) {
    static const risc_diagnostic_source_api_v1 source={1,sizeof(source),nullptr,
        [](void*,uint32_t,char* text,uint32_t capacity,uint32_t* written,uint64_t* sequence,uint32_t* revision)->int32_t{
            if(text && capacity)text[0]=0;
            if(written)*written=0;
            if(sequence)*sequence=0;
            if(revision)*revision=0;
            return RISC_DIAGNOSTIC_SOURCE_ABSENT;
        }};
    return runtime.registerPlatform(RISC_DIAGNOSTIC_SOURCE_CAPABILITY,1,RiscBoot::Runtime::Scope::Global,0,&source);
}
