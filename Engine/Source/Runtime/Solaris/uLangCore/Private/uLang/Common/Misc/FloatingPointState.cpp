// Copyright Epic Games, Inc. All Rights Reserved.

#include "uLang/Common/Misc/FloatingPointState.h"

// mnicolella: copied from AutoRTFM.h
#if (defined(__AUTORTFM) && __AUTORTFM)
#define UE_AUTORTFM 1
#else
#define UE_AUTORTFM 0
#endif

// mnicolella: these forward declarations are a temporary workaround
// for the issue of uLangCore not depending on UE core, which is where
// the AutoRTFM runtime currently resides (#jira SOL-5747)
#if UE_AUTORTFM
extern "C" void autortfm_open(void (*work)(void* arg), void* arg);
extern "C" bool autortfm_is_transactional(void);
extern "C" bool autortfm_is_closed(void);
extern "C" void autortfm_push_on_abort_handler(const void* key, void (*work)(void* arg), void* arg);
extern "C" void autortfm_pop_on_abort_handler(const void* key);
#else
inline void autortfm_open(void (*work)(void* arg), void* arg) { work(arg); }
inline bool autortfm_is_transactional(void) { return false; }
inline bool autortfm_is_closed(void) { return false; }
inline void autortfm_push_on_abort_handler(const void* key, void (*work)(void* arg), void* arg) { }
inline void autortfm_pop_on_abort_handler(const void* key) { }
#endif

// The way to access the control registers, and what should go
// into these control registers, depends on the target
// architecture.

#if defined(_x86_64) || defined(__x86_64__) || defined(_M_AMD64)

#include <pmmintrin.h>

static uint32_t ReadFloatingPointState()
{
    return _mm_getcsr();
}

static void WriteFloatingPointState(uint32_t State)
{
    _mm_setcsr(State);
}

// Our desired state is all floating point exceptions masked, round to nearest, no flush to zero.
static constexpr uint32_t DesiredFloatingPointState = _MM_MASK_MASK | _MM_ROUND_NEAREST | _MM_FLUSH_ZERO_OFF | _MM_DENORMALS_ZERO_OFF;
// Of these fields, we want to check the rounding mode, FTZ and DAZ fields, but don't care about exceptions.
static constexpr uint32_t FloatingPointStateCheckMask = _MM_ROUND_MASK | _MM_FLUSH_ZERO_MASK | _MM_DENORMALS_ZERO_MASK;
// Our problematic state for x86-64 is FTZ enabled, DAZ off (it's SSE3+), rounding mode=RZ
static constexpr uint32_t ProblematicFloatingPointState = _MM_MASK_MASK | _MM_ROUND_TOWARD_ZERO | _MM_FLUSH_ZERO_ON | _MM_DENORMALS_ZERO_OFF;

#elif defined(__aarch64__) || defined(_M_ARM64)

#ifdef _MSC_VER

// With VC++, there are intrinsics
#include <intrin.h>

static uint32_t ReadFloatingPointState()
{
    // The system register read/write instructions use 64-bit registers,
    // but the actual register in AArch64 is defined to be 32-bit in the
    // ARMv8 ARM.
    return (uint32_t)_ReadStatusReg(ARM64_FPCR);
}

static void WriteFloatingPointState(uint32_t State)
{
    _WriteStatusReg(ARM64_FPCR, State);
}

#elif defined(__GNUC__) || defined(__clang__)

static uint32_t ReadFloatingPointState()
{
    uint64_t Value;
    // The system register read/write instructions use 64-bit registers,
    // but the actual register in AArch64 is defined to be 32-bit in the
    // ARMv8 ARM.
    __asm__ volatile("mrs %0, fpcr" : "=r"(Value));
    return (uint32_t)Value;
}

static void WriteFloatingPointState(uint32_t State)
{
    uint64_t State64 = State; // Actual reg is 32b, instruction wants a 64b reg
    __asm__ volatile("msr fpcr, %0" : : "r"(State64));
}

#else

#error Unsupported compiler for AArch64!

#endif // Compiler select

// Conveniently, on AArch64, all exceptions masked, round to nearest, IEEE compliant mode is just 0.
static constexpr uint32_t DesiredFloatingPointState = 0;
// We care about FZ (bit 24) = Flush-To-Zero enable and RMode (bits [23:22]) = rounding mode.
static constexpr uint32_t FloatingPointStateCheckMask = 0x01c00000;
// Our problematic state for AArch64 is FZ enabled and RMode=RZ
static constexpr uint32_t ProblematicFloatingPointState = 0x01c00000;

#else

#error Unrecognized target platform!

#endif // Target select

namespace uLang
{

uint32_t ReadFloatingPointStateAutoRTFMSafe()
{
    uint32_t State = 0;

    // #jira SOL-5747 - make this an AutoRTFM::Open instead.
    autortfm_open([](void* Arg) { *reinterpret_cast<uint32_t*>(Arg) = ReadFloatingPointState(); }, &State);

    return State;
}

void AssertExpectedFloatingPointState()
{
    const uint32_t CurrentState = ReadFloatingPointStateAutoRTFMSafe();
    const uint32_t CurrentStateMasked = CurrentState & FloatingPointStateCheckMask;
    const uint32_t DesiredStateMasked = DesiredFloatingPointState & FloatingPointStateCheckMask;
    ULANG_ASSERTF(CurrentStateMasked == DesiredStateMasked, "Unsupported floating-point state set");
}

void SetProblematicFloatingPointStateForTesting()
{
    // #jira SOL-5747 - make this an AutoRTFM::IsTransactional instead.
    ULANG_ASSERTF(!autortfm_is_transactional(), "Cannot set problematic floating point state in a transaction");

    WriteFloatingPointState(ProblematicFloatingPointState);
}

CFloatStateSaveRestore::CFloatStateSaveRestore()
{
    _SavedState = ReadFloatingPointStateAutoRTFMSafe();

    // #jira SOL-5747 - make this an AutoRTFM::Open instead.
    autortfm_open([](void*) { WriteFloatingPointState(DesiredFloatingPointState); }, nullptr);

    // #jira SOL-5747 - remove this when AutoRTFM::PushOnAbortHandler is used below.
    if (autortfm_is_closed())
    {
        // #jira SOL-5747 - make this an AutoRTFM::PushOnAbortHandler instead.
        // Fine to save the address of _SavedState, because the on-abort handler will only survive until the destructor is called.
        autortfm_push_on_abort_handler(this, [](void* Arg) { WriteFloatingPointState(*reinterpret_cast<uint32_t*>(Arg)); }, &_SavedState);
    }
}

CFloatStateSaveRestore::~CFloatStateSaveRestore()
{
    // #jira SOL-5747 - make this an AutoRTFM::Open instead.
    autortfm_open([](void* Arg) { WriteFloatingPointState(*reinterpret_cast<uint32_t*>(Arg)); }, &_SavedState);

    // #jira SOL-5747 - remove this when AutoRTFM::PopOnAbortHandler is used below.
    if (autortfm_is_closed())
    {
        // #jira SOL-5747 - make this an AutoRTFM::PopOnAbortHandler instead.
        autortfm_pop_on_abort_handler(this);
    }
}

}
