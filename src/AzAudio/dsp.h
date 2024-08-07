/*
	File: dps.h
	Author: Philip Haynes
	structs and functions for digital signal processing
*/

#ifndef AZAUDIO_DSP_H
#define AZAUDIO_DSP_H

#include <stdlib.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AZAUDIO_RMS_SAMPLES 128
#define AZAUDIO_LOOKAHEAD_SAMPLES 128
// The duration of transitions between the variable parameter values
#define AZAUDIO_SAMPLER_TRANSITION_FRAMES 128



// Buffer used by DSP functions for their input/output
typedef struct azaBuffer {
	// actual read/write-able data
	// one frame is a single sample from each channel, one after the other
	float *samples;
	// how many samples there are in a single channel
	size_t frames;
	// distance between samples from one channel in number of floats
	size_t stride;
	// how many channels are stored in this buffer for user-created buffers
	// or how many channels should be accessed by DSP functions
	size_t channels;
	// samples per second, used by DSP functions that rely on timing
	size_t samplerate;
} azaBuffer;
// You must first set frames and channels before calling this to allocate samples.
// If samples are externally-managed, you don't have to do this.
int azaBufferInit(azaBuffer *data);
int azaBufferDeinit(azaBuffer *data);

// Mixes src into the existing contents of dst
void azaBufferMix(azaBuffer dst, float volumeDst, azaBuffer src, float volumeSrc);

// Copies the contents of one channel of src into dst
void azaBufferCopyChannel(azaBuffer dst, size_t channelDst, azaBuffer src, size_t channelSrc);

static inline azaBuffer azaBufferOneSample(float *sample, size_t samplerate) {
	return (azaBuffer) {
		.samples = sample,
		.frames = 1,
		.stride = 1,
		.channels = 1,
		.samplerate = samplerate,
	};
}


typedef enum azaDSPKind {
	AZA_DSP_NONE=0,
	AZA_DSP_RMS,
	AZA_DSP_FILTER,
	AZA_DSP_LOOKAHEAD_LIMITER,
	AZA_DSP_COMPRESSOR,
	AZA_DSP_DELAY,
	AZA_DSP_REVERB,
	AZA_DSP_SAMPLER,
	AZA_DSP_GATE,
} azaDSPKind;

// Generic interface to all the DSP datas
typedef struct azaDSPData {
	azaDSPKind kind;
	uint32_t structSize;
	struct azaDSPData *pNext;
} azaDSPData;
int azaDSP(azaBuffer buffer, azaDSPData *data);


typedef struct azaRmsData {
	azaDSPData header;
	float squared;
	float buffer[AZAUDIO_RMS_SAMPLES];
	int index;
} azaRmsData;
void azaRmsDataInit(azaRmsData *data);
int azaRms(azaBuffer buffer, azaRmsData *data);


typedef enum azaFilterKind {
	AZA_FILTER_HIGH_PASS,
	AZA_FILTER_LOW_PASS,
	AZA_FILTER_BAND_PASS,
} azaFilterKind;

typedef struct azaFilterData {
	azaDSPData header;
	float outputs[2];
	
	// User configuration
	
	azaFilterKind kind;
	// Cutoff frequency in Hz
	float frequency;
	// Blends the effect output with the dry signal where 1 is fully dry and 0 is fully wet.
	float dryMix;
} azaFilterData;
void azaFilterDataInit(azaFilterData *data);
int azaFilter(azaBuffer buffer, azaFilterData *data);



int azaCubicLimiter(azaBuffer buffer);



// NOTE: This limiter increases latency by AZAUDIO_LOOKAHEAD_SAMPLES samples
typedef struct azaLookaheadLimiterData {
	azaDSPData header;
	float gainBuffer[AZAUDIO_LOOKAHEAD_SAMPLES];
	float valBuffer[AZAUDIO_LOOKAHEAD_SAMPLES];
	int index;
	float sum;
	
	// User configuration
	
	// input gain in dB
	float gainInput;
	// output gain in dB (should never peak higher than this)
	float gainOutput;
} azaLookaheadLimiterData;
void azaLookaheadLimiterDataInit(azaLookaheadLimiterData *data);
int azaLookaheadLimiter(azaBuffer buffer, azaLookaheadLimiterData *data);



typedef struct azaCompressorData {
	azaDSPData header;
	azaRmsData rmsData;
	float attenuation;
	float gain; // For monitoring/debugging
	
	// User configuration
	
	// Activation threshold in dB
	float threshold;
	// positive values allow 1/ratio of the overvolume through
	// negative values subtract overvolume*ratio
	float ratio;
	// attack time in ms
	float attack;
	// decay time in ms
	float decay;
} azaCompressorData;
void azaCompressorDataInit(azaCompressorData *data);
int azaCompressor(azaBuffer buffer, azaCompressorData *data);



typedef struct azaDelayData {
	azaDSPData header;
	float *buffer; // Must be dynamically-allocated to allow different time spans
	size_t capacity;
	// Needs to be kept track of to handle the resizing of buffer gracefully
	size_t delaySamples;
	size_t index;
	
	// User configuration
	
	// effect gain in dB
	float gain;
	// dry gain in dB
	float gainDry;
	// delay time in ms
	float delay;
	// 0 to 1 multiple of output feeding back into input
	float feedback;
	// You can provide a chain of effects to operate on the wet output
	azaDSPData *wetEffects;
} azaDelayData;
void azaDelayDataInit(azaDelayData *data);
void azaDelayDataDeinit(azaDelayData *data);
int azaDelay(azaBuffer buffer, azaDelayData *data);



#define AZAUDIO_REVERB_DELAY_COUNT 15
typedef struct azaReverbData {
	azaDSPData header;
	azaDelayData delayDatas[AZAUDIO_REVERB_DELAY_COUNT];
	azaFilterData filterDatas[AZAUDIO_REVERB_DELAY_COUNT];
	
	// User configuration
	
	// effect gain in dB
	float gain;
	// dry gain in dB
	float gainDry;
	// value affecting reverb feedback, roughly in the range of 1 to 100 for reasonable results
	float roomsize;
	// value affecting damping of high frequencies, roughly in the range of 1 to 5
	float color;
	// delay for first reflections in ms
	float delay;
} azaReverbData;
void azaReverbDataInit(azaReverbData *data);
void azaReverbDataDeinit(azaReverbData *data);
int azaReverb(azaBuffer buffer, azaReverbData *data);



typedef struct azaSamplerData {
	azaDSPData header;
	float frame;
	float s; // Smooth speed
	float g; // Smooth gain
	
	// User configuration
	
	// buffer containing the sound we're sampling
	azaBuffer *buffer;
	// playback speed as a multiple where 1 is full speed
	float speed;
	// volume of effect in dB
	float gain;
} azaSamplerData;
int azaSamplerDataInit(azaSamplerData *data);
int azaSampler(azaBuffer buffer, azaSamplerData *data);



typedef struct azaGateData {
	azaDSPData header;
	azaRmsData rms;
	float attenuation;
	float gain;
	
	// User configuration
	
	// cutoff threshold in dB
	float threshold;
	// attack time in ms
	float attack;
	// decay time in ms
	float decay;
	// Any effects to apply to the activation signal
	azaDSPData *activationEffects;
} azaGateData;
void azaGateDataInit(azaGateData *data);
int azaGate(azaBuffer buffer, azaGateData *data);



#ifdef __cplusplus
}
#endif

#endif // AZAUDIO_DSP_H