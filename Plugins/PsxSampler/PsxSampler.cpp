#include "PsxSampler.h"

#include "IPlug_include_in_plug_src.h"
#include "LFO.h"

static constexpr uint32_t   kSpuRamSize     = 512 * 1024;   // SPU RAM size: this is the size that the PS1 had
static constexpr uint32_t   kSpuNumVoices   = 24;           // Maximum number of SPU voices: this is the hardware limit of the PS1
static constexpr int        kNumPresets     = 1;            // Not doing any actual presets for this instrument

//------------------------------------------------------------------------------------------------------------------------------------------
// Initializes the sampler instrument plugin
//------------------------------------------------------------------------------------------------------------------------------------------
PsxSampler::PsxSampler(const InstanceInfo& info) noexcept
    : Plugin(info, MakeConfig(kNumParams, kNumPresets))
    , mSpu()
    , mSpuMutex()
    , mNumSampleBlocks(0)
    , mDSP{16}
    , mMeterSender()
{
    DefinePluginParams();
    DoEditorSetup();
}

void PsxSampler::ProcessBlock(sample** inputs, sample** outputs, int nFrames) noexcept {
    // TODO...
    mDSP.ProcessBlock(nullptr, outputs, 2, nFrames, mTimeInfo.mPPQPos, mTimeInfo.mTransportIsRunning);
    mMeterSender.ProcessBlock(outputs, nFrames, kCtrlTagMeter);
}

void PsxSampler::OnIdle() noexcept {
    mMeterSender.TransmitData(*this);
}

void PsxSampler::OnReset() noexcept {
    // TODO...
    mDSP.Reset(GetSampleRate(), GetBlockSize());
}

void PsxSampler::ProcessMidiMsg(const IMidiMsg& msg) noexcept {
    // TODO...
    int status = msg.StatusMsg();
    
    switch (status) {
        case IMidiMsg::kNoteOn:
        case IMidiMsg::kNoteOff:
        case IMidiMsg::kPolyAftertouch:
        case IMidiMsg::kControlChange:
        case IMidiMsg::kProgramChange:
        case IMidiMsg::kChannelAftertouch:
        case IMidiMsg::kPitchWheel:
            mDSP.ProcessMidiMsg(msg);
            break;
        
        default:
            break;
    }
}

void PsxSampler::OnParamChange(int paramIdx) noexcept {
    // TODO...
    // mDSP.SetParam(paramIdx, GetParam(paramIdx)->Value());
}

bool PsxSampler::OnMessage(int msgTag, int ctrlTag, int dataSize, const void* pData) noexcept {
    // TODO...
    if ((ctrlTag == kCtrlTagBender) && (msgTag == IWheelControl::kMessageTagSetPitchBendRange)) {
        const int bendRange = *static_cast<const int*>(pData);
        mDSP.mSynth.SetPitchBendRange(bendRange);
    }
    
    return false;
}

//------------------------------------------------------------------------------------------------------------------------------------------
// Defines the parameters used by the plugin
//------------------------------------------------------------------------------------------------------------------------------------------
void PsxSampler::DefinePluginParams() noexcept {
    GetParam(kParamSampleRate)->InitInt("sampleRate", 11025, 1, INT32_MAX, "", IParam::EFlags::kFlagMeta);          // Influences 'baseNote'
    GetParam(kParamBaseNote)->InitDouble("baseNote", 84, 0.00001, 10000.0, 0.125, "", IParam::EFlags::kFlagMeta);   // Influences 'sampleRate'
    GetParam(kParamLengthInSamples)->InitInt("lengthInSamples", 0, 0, INT32_MAX);
    GetParam(kParamLengthInBlocks)->InitInt("lengthInBlocks", 0, 0, INT32_MAX);
    GetParam(kParamLoopStartSample)->InitInt("loopStartSample", 0, 0, INT32_MAX);
    GetParam(kParamLoopEndSample)->InitInt("loopEndSample", 0, 0, INT32_MAX);
    GetParam(kParamVolume)->InitInt("volume", 127, 0, 127);
    GetParam(kParamPan)->InitInt("pan", 64, 0, 127);
    GetParam(kParamPitchstepUp)->InitInt("pitchstepUp", 1, 0, 36);
    GetParam(kParamPitchstepDown)->InitInt("pitchstepDown", 1, 0, 36);
    GetParam(kParamAttackStep)->InitInt("attackStep", 3, 0, 3);
    GetParam(kParamAttackShift)->InitInt("attackShift", 0, 0, 31);
    GetParam(kParamAttackIsExp)->InitInt("attackIsExp", 0, 0, 1);
    GetParam(kParamDecayShift)->InitInt("decayShift", 0, 0, 15);
    GetParam(kParamSustainLevel)->InitInt("sustainLevel", 15, 0, 15);
    GetParam(kParamSustainStep)->InitInt("sustainStep", 0, 0, 3);
    GetParam(kParamSustainShift)->InitInt("sustainShift", 31, 0, 31);
    GetParam(kParamSustainDec)->InitInt("sustainDec", 0, 0, 1);
    GetParam(kParamSustainIsExp)->InitInt("sustainIsExp", 1, 0, 1);
    GetParam(kParamReleaseShift)->InitInt("releaseShift", 0, 0, 31);
    GetParam(kParamReleaseIsExp)->InitInt("releaseIsExp", 0, 0, 1);
}

//------------------------------------------------------------------------------------------------------------------------------------------
// Setup controls for the plugin's GUI
//------------------------------------------------------------------------------------------------------------------------------------------
void PsxSampler::DoEditorSetup() noexcept {
    mMakeGraphicsFunc = [&]() {
        return MakeGraphics(*this, PLUG_WIDTH, PLUG_HEIGHT, PLUG_FPS, GetScaleForScreen(PLUG_HEIGHT));
    };

    mLayoutFunc = [&](IGraphics* pGraphics) {
        // High level GUI setup
        pGraphics->AttachCornerResizer(EUIResizerMode::Scale, false);
        pGraphics->AttachPanelBackground(COLOR_GRAY);
        pGraphics->EnableMouseOver(true);
        pGraphics->EnableMultiTouch(true);
        pGraphics->LoadFont("Roboto-Regular", ROBOTO_FN);

        // Styles
        const IVStyle labelStyle =
            DEFAULT_STYLE
            .WithDrawFrame(false)
            .WithDrawShadows(false)
            .WithValueText(
                DEFAULT_TEXT
                .WithVAlign(EVAlign::Middle)
                .WithAlign(EAlign::Near)
                .WithSize(18.0f)
            );
        
        const IText editBoxTextStyle = DEFAULT_TEXT;
        const IColor editBoxBgColor = IColor(255, 255, 255, 255);

        // Setup the panels
        const IRECT bndPadded = pGraphics->GetBounds().GetPadded(-10.0f);
        const IRECT bndSamplePanel = bndPadded.GetFromTop(80).GetFromLeft(300);
        const IRECT bndSampleInfoPanel = bndPadded.GetFromTop(80).GetReducedFromLeft(310).GetFromLeft(510);
        const IRECT bndTrackPanel = bndPadded.GetReducedFromTop(90).GetFromTop(100).GetFromLeft(410);
        const IRECT bndEnvelopePanel = bndPadded.GetReducedFromTop(200).GetFromTop(230).GetFromLeft(860);

        pGraphics->AttachControl(new IVGroupControl(bndSamplePanel, "Sample"));
        pGraphics->AttachControl(new IVGroupControl(bndSampleInfoPanel, "Sample Info"));
        pGraphics->AttachControl(new IVGroupControl(bndTrackPanel, "Track"));
        pGraphics->AttachControl(new IVGroupControl(bndEnvelopePanel, "Envelope"));

        // Make a read only edit box
        const auto makeReadOnlyEditBox = [=](const IRECT bounds, const int paramIdx) noexcept {
            const IText textStyle =
                editBoxTextStyle
                .WithFGColor(IColor(255, 255, 255, 255))
                .WithSize(20.0f)
                .WithAlign(EAlign::Near);

            ICaptionControl* const pCtrl = new ICaptionControl(bounds, paramIdx, textStyle, IColor(0, 0, 0, 0), false);
            pCtrl->SetDisabled(true);
            pCtrl->DisablePrompt(true);
            pCtrl->SetBlend(IBlend(EBlend::Default, 1.0f));
            return pCtrl;
        };

        // Sample panel
        {
            const IRECT bndPanelPadded = bndSamplePanel.GetReducedFromTop(20.0f);
            const IRECT bndColLoadSave = bndPanelPadded.GetFromLeft(100.0f);
            const IRECT bndColRateNoteLabels = bndPanelPadded.GetReducedFromLeft(110.0f).GetFromLeft(100.0f);
            const IRECT bndColRateNoteValues = bndPanelPadded.GetReducedFromLeft(210.0f).GetFromLeft(80.0f).GetPadded(-4.0f);
            
            pGraphics->AttachControl(new IVButtonControl(bndColLoadSave.GetFromTop(30.0f), SplashClickActionFunc, "Save"));
            pGraphics->AttachControl(new IVButtonControl(bndColLoadSave.GetFromBottom(30.0f), SplashClickActionFunc, "Load"));
            pGraphics->AttachControl(new IVLabelControl(bndColRateNoteLabels.GetFromTop(30.0f), "Sample Rate", labelStyle));
            pGraphics->AttachControl(new IVLabelControl(bndColRateNoteLabels.GetFromBottom(30.0f), "Base Note", labelStyle));
            pGraphics->AttachControl(new ICaptionControl(bndColRateNoteValues.GetFromTop(20.0f), kParamSampleRate, editBoxTextStyle, editBoxBgColor, false));
            pGraphics->AttachControl(new ICaptionControl(bndColRateNoteValues.GetFromBottom(20.0f), kParamBaseNote, editBoxTextStyle, editBoxBgColor, false));
        }

        // Sample info panel
        {
            const IRECT bndPanelPadded = bndSampleInfoPanel.GetReducedFromTop(20.0f);
            const IRECT bndColLengthLabels = bndPanelPadded.GetReducedFromLeft(10.0f).GetFromLeft(130.0f);
            const IRECT bndColLengthValues = bndPanelPadded.GetReducedFromLeft(150.0f).GetFromLeft(100).GetPadded(-4.0f);
            const IRECT bndColLoopLabels = bndPanelPadded.GetReducedFromLeft(260.0f).GetFromLeft(130.0f);
            const IRECT bndColLoopValues = bndPanelPadded.GetReducedFromLeft(400.0f).GetFromLeft(100.0f).GetPadded(-4.0f);

            pGraphics->AttachControl(new IVLabelControl(bndColLengthLabels.GetFromTop(30.0f), "Length (samples)", labelStyle));
            pGraphics->AttachControl(new IVLabelControl(bndColLengthLabels.GetFromBottom(30.0f), "Length (blocks)", labelStyle));
            pGraphics->AttachControl(makeReadOnlyEditBox(bndColLengthValues.GetFromTop(20.0f), kParamLengthInSamples));
            pGraphics->AttachControl(makeReadOnlyEditBox(bndColLengthValues.GetFromBottom(20.0f), kParamLengthInBlocks));
            pGraphics->AttachControl(new IVLabelControl(bndColLoopLabels.GetFromTop(30.0f), "Loop Start Sample", labelStyle));
            pGraphics->AttachControl(new IVLabelControl(bndColLoopLabels.GetFromBottom(30.0f), "Loop End Sample", labelStyle));
            pGraphics->AttachControl(makeReadOnlyEditBox(bndColLoopValues.GetFromTop(20.0f), kParamLoopStartSample));
            pGraphics->AttachControl(makeReadOnlyEditBox(bndColLoopValues.GetFromBottom(20.0f), kParamLoopEndSample));
        }

        // Track Panel
        {
            const IRECT bndPanelPadded = bndTrackPanel.GetReducedFromTop(24.0f).GetReducedFromBottom(4.0f);
            const IRECT bndColVol = bndPanelPadded.GetFromLeft(80.0f);
            const IRECT bndColPan = bndPanelPadded.GetReducedFromLeft(80.0f).GetFromLeft(80.0f);
            const IRECT bndColPStepUp = bndPanelPadded.GetReducedFromLeft(160.0f).GetFromLeft(120.0f);
            const IRECT bndColPStepDown = bndPanelPadded.GetReducedFromLeft(280.0f).GetFromLeft(120.0f);

            pGraphics->AttachControl(new IVKnobControl(bndColVol, kParamVolume, "Volume", DEFAULT_STYLE, true));
            pGraphics->AttachControl(new IVKnobControl(bndColPan, kParamPan, "Pan", DEFAULT_STYLE, true));
            pGraphics->AttachControl(new IVKnobControl(bndColPStepUp, kParamPitchstepUp, "Pitchstep Up", DEFAULT_STYLE, true));
            pGraphics->AttachControl(new IVKnobControl(bndColPStepDown, kParamPitchstepDown, "Pitchstep Down", DEFAULT_STYLE, true));
        }

        // Envelope Panel
        {
            const IRECT bndPanelPadded = bndEnvelopePanel.GetReducedFromTop(30.0f).GetReducedFromBottom(4.0f);
            const IRECT bndCol1 = bndPanelPadded.GetFromLeft(120.0f);
            const IRECT bndCol2 = bndPanelPadded.GetReducedFromLeft(120.0f).GetFromLeft(120.0f);
            const IRECT bndCol3 = bndPanelPadded.GetReducedFromLeft(240.0f).GetFromLeft(120.0f);
            const IRECT bndCol4 = bndPanelPadded.GetReducedFromLeft(360.0f).GetFromLeft(120.0f);
            const IRECT bndCol5 = bndPanelPadded.GetReducedFromLeft(480.0f).GetFromLeft(120.0f);
            const IRECT bndCol6 = bndPanelPadded.GetReducedFromLeft(600.0f).GetFromLeft(120.0f);
            const IRECT bndCol7 = bndPanelPadded.GetReducedFromLeft(720.0f).GetFromLeft(120.0f);

            pGraphics->AttachControl(new IVKnobControl(bndCol1.GetFromTop(80.0f), kParamAttackStep, "Attack Step", DEFAULT_STYLE, true));
            pGraphics->AttachControl(new IVKnobControl(bndCol1.GetReducedFromTop(100.0f).GetFromTop(80.0f), kParamAttackShift, "Attack Shift", DEFAULT_STYLE, true));
            pGraphics->AttachControl(new IVKnobControl(bndCol2.GetFromTop(80.0f), kParamAttackIsExp, "Attack Is Exp.", DEFAULT_STYLE, true));
            pGraphics->AttachControl(new IVKnobControl(bndCol3.GetFromTop(80.0f), kParamDecayShift, "Decay Shift", DEFAULT_STYLE, true));
            pGraphics->AttachControl(new IVKnobControl(bndCol4.GetFromTop(80.0f), kParamSustainLevel, "Sustain Level", DEFAULT_STYLE, true));
            pGraphics->AttachControl(new IVKnobControl(bndCol5.GetFromTop(80.0f), kParamSustainStep, "Sustain Step", DEFAULT_STYLE, true));
            pGraphics->AttachControl(new IVKnobControl(bndCol5.GetReducedFromTop(100.0f).GetFromTop(80.0f), kParamSustainShift, "Sustain Shift", DEFAULT_STYLE, true));
            pGraphics->AttachControl(new IVKnobControl(bndCol6.GetFromTop(80.0f), kParamSustainDec, "Sustain Dec.", DEFAULT_STYLE, true));
            pGraphics->AttachControl(new IVKnobControl(bndCol6.GetReducedFromTop(100.0f).GetFromTop(80.0f), kParamSustainIsExp, "Sustain Is Exp.", DEFAULT_STYLE, true));
            pGraphics->AttachControl(new IVKnobControl(bndCol7.GetFromTop(80.0f), kParamReleaseShift, "Release Shift", DEFAULT_STYLE, true));
            pGraphics->AttachControl(new IVKnobControl(bndCol7.GetReducedFromTop(100.0f).GetFromTop(80.0f), kParamReleaseIsExp, "Release Is Exp.", DEFAULT_STYLE, true));
        }

        // Add the test keyboard and pitch bend wheel
        const IRECT bndKeyboardPanel = bndPadded.GetFromBottom(200);
        const IRECT bndKeyboard = bndKeyboardPanel.GetReducedFromLeft(60.0f);
        const IRECT bndPitchWheel = bndKeyboardPanel.GetFromLeft(50.0f);

        pGraphics->AttachControl(new IWheelControl(bndPitchWheel), kCtrlTagBender);
        pGraphics->AttachControl(new IVKeyboardControl(bndKeyboard), kCtrlTagKeyboard);

        // Add the volume meter
        const IRECT bndVolMeter = bndPadded.GetReducedFromTop(10).GetFromRight(30).GetFromTop(180);
        pGraphics->AttachControl(new IVLEDMeterControl<2>(bndVolMeter), kCtrlTagMeter);

        // Allow Qwerty keyboard - but only in standalone mode.
        // In VST mode the host might have it's own keyboard input functionality, and this could interfere...
        #if APP_API
          pGraphics->SetQwertyMidiKeyHandlerFunc(
              [pGraphics](const IMidiMsg& msg) noexcept {
                  dynamic_cast<IVKeyboardControl*>(pGraphics->GetControlWithTag(kCtrlTagKeyboard))->SetNoteFromMidi(msg.NoteNumber(), msg.StatusMsg() == IMidiMsg::kNoteOn);
              }
          );
        #endif
    };
}

//------------------------------------------------------------------------------------------------------------------------------------------
// Setup DSP related stuff
//------------------------------------------------------------------------------------------------------------------------------------------
void PsxSampler::DoDspSetup() noexcept {
    // Create the PlayStation SPU core
    Spu::initCore(mSpu, kSpuRamSize, kSpuNumVoices);

    // Set default volume levels
    mSpu.masterVol.left = 0x3FFF;
    mSpu.masterVol.right = 0x3FFF;
    mSpu.reverbVol.left = 0;
    mSpu.reverbVol.right = 0;
    mSpu.extInputVol.left = 0;
    mSpu.extInputVol.right = 0;

    // Setup other SPU settings
    mSpu.bUnmute = true;
    mSpu.bReverbWriteEnable = false;
    mSpu.bExtEnabled = false;
    mSpu.bExtReverbEnable = false;
    mSpu.pExtInputCallback = nullptr;
    mSpu.pExtInputUserData = nullptr;
    mSpu.cycleCount = 0;
    mSpu.reverbBaseAddr8 = (kSpuRamSize / 8) - 1;   // Allocate no RAM for reverb: this instrument does not use the PSX reverb effects
    mSpu.reverbCurAddr = 0;
    mSpu.processedReverb = {};
    mSpu.reverbRegs = {};

    // Terminate the current sample in SPU RAM
    AddSampleTerminator();
}

//------------------------------------------------------------------------------------------------------------------------------------------
// Called when a parameter changes
//------------------------------------------------------------------------------------------------------------------------------------------
void PsxSampler::InformHostOfParamChange([[maybe_unused]] int idx, [[maybe_unused]] double normalizedValue) noexcept {
    UpdateSpuVoicesFromParams();
}

//------------------------------------------------------------------------------------------------------------------------------------------
// Called when a preset changes
//------------------------------------------------------------------------------------------------------------------------------------------
void PsxSampler::OnRestoreState() noexcept {
    // TODO...
    Plugin::OnRestoreState();
    UpdateSpuVoicesFromParams();
    AddSampleTerminator();
}

void PsxSampler::UpdateSpuVoicesFromParams() noexcept {
    // TODO...
    std::lock_guard<std::recursive_mutex> lockSpu(mSpuMutex);
}

//------------------------------------------------------------------------------------------------------------------------------------------
// Add a terminator for the currently loaded sample consisting of two silent ADPCM blocks which will loop indefinitely.
// Used to guarantee a sound will stop playing after it reaches the end, since SPU voices technically never stop.
// The SPU emulation however will kill them to save on CPU time...
//------------------------------------------------------------------------------------------------------------------------------------------
void PsxSampler::AddSampleTerminator() noexcept {
    // Figure out which ADPCM sample block to write the terminators
    constexpr uint32_t kMaxSampleBlocks = kSpuRamSize / Spu::ADPCM_BLOCK_SIZE;
    static_assert(kMaxSampleBlocks >= 2);
    const uint32_t termAdpcmBlocksStartIdx = std::min(mNumSampleBlocks, kMaxSampleBlocks - 2);
    std::byte* const pTermAdpcmBlocks = mSpu.pRam + (size_t) Spu::ADPCM_BLOCK_SIZE * termAdpcmBlocksStartIdx;

    // Zero the bytes for the two ADPCM sample blocks firstly
    std::memset(pTermAdpcmBlocks, 0, Spu::ADPCM_BLOCK_SIZE * 2);

    // The 2nd byte of each ADPCM block is the flags byte, and is where we indicate loop start/end.
    // Make the first block be the loop start, and the second block be loop end:
    pTermAdpcmBlocks[1]   = (std::byte) Spu::ADPCM_FLAG_LOOP_START;
    pTermAdpcmBlocks[17]  = (std::byte) Spu::ADPCM_FLAG_LOOP_END;
}
