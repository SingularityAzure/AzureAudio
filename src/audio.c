/*
    File: audio.c
    Author: singularity
*/

#include "audio.h"

#include <portaudio.h>
#include <math.h>
#include <stdlib.h>

#ifndef AZURE_AUDIO_NO_STDIO
#include <stdio.h>
#else
#ifndef NULL
#define NULL 0
#endif
#endif

// Helper functions

float trif(float x) {
    x /= 3.1415926535;
    while (x < 0)
        x += 4;
    while (x > 4)
        x -= 4;
    if (x > 3)
        return 4.0 - x;
    if (x < 1)
        return -x;
    return x - 2.0;
}

float sqrf(float x) {
    x /= 3.1415926535;
    while (x < 0)
        x += 2;
    while (x > 2)
        x -= 2;
    if (x > 1)
        return 1;
    return -1;
}

float sinc(float x) {
    if (x == 0)
        return 1.0f;
    float temp = x * 3.1415926535;
    return sinf(temp) / x;
}

float cosc(float x) {
    if (x < -1.0 || x > 1.0)
        return 0.0;
    return cosf(x * 3.1415926535) * 0.5 + 0.5;
}

float linc(float x) {
    if (x > 1.0f || x < -1.0f)
    return 0.0f;
    if (x > 0)
        return 1.0f - x;
    else
        return 1.0f + x;
}

float cubic(float a, float b, float c, float d, float x) {
    return b + 0.5 * x * (c - a + x * (2 * a - 5 * b + 4 * c - d + x * (3 * (b - c) + d - a)));
}

int azaError;

fpLogCallback azaPrint;

fpMixCallback azaMix;
void *azaMixData;
azaDefaultMixData azaDefaultMixFuncData;

int azaInit() {
    azaPrint = azaDefaultLogFunc;
    azaMix = azaDefaultMixFunc;
    azaMixData = &azaDefaultMixFuncData;
    //inputBuffer = NULL;
    //outputBuffer = NULL;
    azaError = AZA_SUCCESS;
    return azaError;
}

int azaClean() {
    azaError = AZA_SUCCESS;
    return azaError;
}

int azaGetError() {
    return azaError;
}

void azaDefaultLogFunc(const char* message) {
    #ifndef AZURE_AUDIO_NO_STDIO
    printf("AzureAudio: %s\n",message);
    #endif
}

int azaDefaultMixFunc(const float *input, float *output, unsigned long frames, int channels, void *userData) {
    azaDefaultMixData *mixData = (azaDefaultMixData*)userData;
    //if (rand()%200 == 0) {
        for (int i = 0; i < channels; i++) {
            mixData->samplerData[i].speed += 0.001;
        }
    //}
    if (azaSampler(NULL, output, mixData->samplerData, frames, channels)) {
        return azaError;
    }
    //if (azaReverb(output, output, mixData->reverbData, frames, channels)) {
    //    return azaError;
    //}
    //if (azaHighPass(output, output, mixData->highPassData, frames, channels)) {
    //    return azaError;
    //}
    if (azaCompressor(output, output, mixData->compressorData, frames, channels)) {
        return azaError;
    }
    if (azaLookaheadLimiter(output, output, mixData->limiterData, frames, channels)) {
        return azaError;
    }
    azaError = AZA_SUCCESS;
    return azaError;
}

int azaSetLogCallback(fpLogCallback newLogFunc) {
    if (newLogFunc != NULL) {
        azaPrint = newLogFunc;
    } else {
        azaPrint = azaDefaultLogFunc;
    }
    azaError = AZA_SUCCESS;
    return azaError;
}

int azaSetMixCallback(fpMixCallback newMixFunc) {
    if (newMixFunc != NULL) {
        azaMix = newMixFunc;
    } else {
        azaMix = azaDefaultMixFunc;
    }
    azaError = AZA_SUCCESS;
    return azaError;
}

int azaBufferInit(azaBuffer *data) {
    if (data->frames < 1) {
        azaError = AZA_ERROR_INVALID_FRAME_COUNT;
        data->samples = NULL;
        return azaError;
    }
    data->samples = (float*)malloc(sizeof(float) * data->frames);
    azaError = AZA_SUCCESS;
    return azaError;
}

int azaBufferClean(azaBuffer *data) {
    if (data->samples != NULL) {
        free(data->samples);
        azaError = AZA_SUCCESS;
        return azaError;
    }
    azaError = AZA_ERROR_NULL_POINTER;
    return azaError;
}

void azaRmsDataInit(azaRmsData *data) {
    data->squared = 0.0f;
    for (int i = 0; i < AZURE_AUDIO_RMS_SAMPLES; i++) {
        data->buffer[i] = 0.0f;
    }
    data->index = 0;
}

void azaLookaheadLimiterDataInit(azaLookaheadLimiterData *data) {
    for (int i = 0; i < AZURE_AUDIO_LOOKAHEAD_SAMPLES; i++) {
        data->gainBuffer[i] = 0.0f;
        data->valBuffer[i] = 0.0f;
    }
}

void azaLowPassDataInit(azaLowPassData *data) {
    data->output = 0.0f;
}

void azaHighPassDataInit(azaHighPassData *data) {
    data->output = 0.0f;
}

void azaCompressorDataInit(azaCompressorData *data) {
    azaRmsDataInit(&data->rms);
    data->attenuation = 0.0f;
}

void azaDelayDataInit(azaDelayData *data) {
    data->buffer = (float*)malloc(sizeof(float) * data->samples);
    for (int i = 0; i < data->samples; i++) {
        data->buffer[i] = 0.0f;
    }
    data->index = 0;
}

void azaDelayDataClean(azaDelayData *data) {
    free(data->buffer);
}

void azaReverbDataInit(azaReverbData *data) {
    int samples[AZURE_AUDIO_REVERB_DELAY_COUNT] = {1557, 1617, 1491, 1422, 1277, 1356, 1188, 1116, 2111, 2133, 225, 556, 441, 341, 713};
    for (int i = 0; i < AZURE_AUDIO_REVERB_DELAY_COUNT; i++) {
        data->delay[i].samples = samples[i] + data->samplesOffset;
        azaDelayDataInit(&data->delay[i]);
        azaLowPassDataInit(&data->lowPass[i]);
    }
}

int azaSamplerDataInit(azaSamplerData *data) {
    if (data->buffer == NULL) {
        azaError = AZA_ERROR_NULL_POINTER;
        azaPrint("Sampler initialized without a buffer!");
        return azaError;
    }
    data->frame = 0;
    data->s = 0.0f; // Starting at zero ensures click-free playback no matter what
    data->g = 0.0f;
    azaError = AZA_SUCCESS;
    return azaError;
}

void azaReverbDataClean(azaReverbData *data) {
    for (int i = 0; i < AZURE_AUDIO_REVERB_DELAY_COUNT; i++) {
        azaDelayDataClean(&data->delay[i]);
    }
}

int azaDefaultMixDataInit(azaDefaultMixData *data) {
    if (data->channels < 1) {
        azaError = AZA_ERROR_INVALID_CHANNEL_COUNT;
        data->highPassData = NULL;
        data->compressorData = NULL;
        data->limiterData = NULL;
        return azaError;
    }
    data->buffer.frames = 1000;
    azaBufferInit(&data->buffer);
    for (int i = 0; i < 1000; i++) {
        data->buffer.samples[i] = trif(((float)i) * 3.1415926535 / 5.0f);
    }
    data->samplerData = (azaSamplerData*)malloc(sizeof(azaSamplerData) * data->channels);
    data->highPassData = (azaHighPassData*)malloc(sizeof(azaHighPassData) * data->channels);
    data->reverbData = (azaReverbData*)malloc(sizeof(azaReverbData) * data->channels);
    data->compressorData = (azaCompressorData*)malloc(sizeof(azaCompressorData) * data->channels);
    data->limiterData = (azaLookaheadLimiterData*)malloc(sizeof(azaLookaheadLimiterData) * data->channels);
    for (int i = 0; i < data->channels; i++) {
        data->samplerData[i].buffer = &data->buffer;
        data->samplerData[i].gain = 1.0f;
        data->samplerData[i].speed = 0.0f;
        azaSamplerDataInit(&data->samplerData[i]);

        data->reverbData[i].amount = 0.0;
        data->reverbData[i].color = 0.5;
        data->reverbData[i].roomsize = 1.0;
        data->reverbData[i].samplesOffset = i * 23;
        azaReverbDataInit(&data->reverbData[i]);

        data->highPassData[i].samplerate = 44100.0f;
        data->highPassData[i].frequency = 20.0f;
        azaHighPassDataInit(&data->highPassData[i]);

        data->compressorData[i].samplerate = 44100.0f;
        data->compressorData[i].threshold = -18.0f;
        data->compressorData[i].ratio = 4.0f;
        data->compressorData[i].attack = 50.0f;
        data->compressorData[i].decay = 200.0f;
        azaCompressorDataInit(&data->compressorData[i]);

        data->limiterData[i].gain = 0.0f;
        azaLookaheadLimiterDataInit(&data->limiterData[i]);
    }
    azaError = AZA_SUCCESS;
    return azaError;
}

int azaDefaultMixDataClean(azaDefaultMixData *data) {
    if (data->channels < 1) {
        azaError = AZA_ERROR_INVALID_CHANNEL_COUNT;
        return azaError;
    }
    if (data->samplerData != NULL && data->highPassData != NULL && data->compressorData != NULL && data->limiterData != NULL) {
        for (int i = 0; i < data->channels; i++) {
            azaReverbDataClean(&data->reverbData[i]);
        }
        free(data->samplerData);
        free(data->highPassData);
        free(data->reverbData);
        free(data->compressorData);
        free(data->limiterData);
    }
    azaBufferClean(&data->buffer);
    azaError = AZA_SUCCESS;
    return azaError;
}

int azaCubicLimiter(const float *input, float *output, int frames, int channels) {
    if (input == NULL || output == NULL) {
        azaError = AZA_ERROR_NULL_POINTER;
        return azaError;
    }
    if (channels < 1) {
        azaError = AZA_ERROR_INVALID_CHANNEL_COUNT;
        return azaError;
    }
    if (frames < 1) {
        azaError = AZA_ERROR_INVALID_FRAME_COUNT;
        return azaError;
    }
    for (int i = 0; i < frames*channels; i++) {
        if (input[i] > 1.0f)
            output[i] = 1.0f;
        else if (input[i] < -1.0f)
            output[i] = -1.0f;
        else
            output[i] = input[i];
        output[i] = 1.5 * output[i] - 0.5f * output[i] * output[i] * output[i];
    }
    azaError = AZA_SUCCESS;
    return azaError;
}

int azaRms(const float *input, float *output, azaRmsData *data, int frames, int channels) {
    if (input == NULL || output == NULL || data == NULL) {
        azaError = AZA_ERROR_NULL_POINTER;
        return azaError;
    }
    if (channels < 1) {
        azaError = AZA_ERROR_INVALID_CHANNEL_COUNT;
        return azaError;
    }
    if (frames < 1) {
        azaError = AZA_ERROR_INVALID_FRAME_COUNT;
        return azaError;
    }
    for (int i = 0; i < frames*channels; i++) {
        azaRmsData *datum = &data[i % channels];

        datum->squared -= datum->buffer[datum->index];
        datum->buffer[datum->index] = input[i] * input[i];
        datum->squared += datum->buffer[datum->index++];
        if (datum->index >= AZURE_AUDIO_RMS_SAMPLES)
            datum->index = 0;

        output[i] = sqrtf(datum->squared/AZURE_AUDIO_RMS_SAMPLES);
    }
    azaError = AZA_SUCCESS;
    return azaError;
}

int azaLookaheadLimiter(const float *input, float *output, azaLookaheadLimiterData *data, int frames, int channels) {
    if (input == NULL || output == NULL || data == NULL) {
        azaError = AZA_ERROR_NULL_POINTER;
        return azaError;
    }
    if (channels < 1) {
        azaError = AZA_ERROR_INVALID_CHANNEL_COUNT;
        return azaError;
    }
    if (frames < 1) {
        azaError = AZA_ERROR_INVALID_FRAME_COUNT;
        return azaError;
    }
    for (int i = 0; i < frames*channels; i++) {
        azaLookaheadLimiterData *datum = &data[i % channels];

        float peak = input[i];
        float gain = datum->gain;
        if (peak < 0.0f)
            peak = -peak;
        peak = log2f(peak)*6.0f + gain;
        if (peak < 0.0f)
            peak = 0.0f;
        datum->sum += peak - datum->gainBuffer[datum->index];
        float average = datum->sum / AZURE_AUDIO_LOOKAHEAD_SAMPLES;
        if (average > peak) {
            datum->sum += average - peak;
            peak = average;
        }
        datum->gainBuffer[datum->index] = peak;

        datum->valBuffer[datum->index] = input[i];

        datum->index = (datum->index+1)%AZURE_AUDIO_LOOKAHEAD_SAMPLES;

        if (average > datum->gainBuffer[datum->index])
            gain -= average;
        else
            gain -= datum->gainBuffer[datum->index];
        float out = datum->valBuffer[datum->index] * powf(2.0f,gain/6.0f);
        if (out < -1.0f)
            out = -1.0f;
        else if (out > 1.0f)
            out = 1.0f;
        output[i] = out;
    }
    azaError = AZA_SUCCESS;
    return azaError;
}

int azaLowPass(const float *input, float *output, azaLowPassData *data, int frames, int channels) {
    if (input == NULL || output == NULL || data == NULL) {
        azaError = AZA_ERROR_NULL_POINTER;
        return azaError;
    }
    if (channels < 1) {
        azaError = AZA_ERROR_INVALID_CHANNEL_COUNT;
        return azaError;
    }
    if (frames < 1) {
        azaError = AZA_ERROR_INVALID_FRAME_COUNT;
        return azaError;
    }

    for (int i = 0; i < frames*channels; i++) {
        azaLowPassData *datum = &data[i % channels];

        float amount = expf(-1.0f * (datum->frequency / datum->samplerate));
        datum->output = input[i] + amount * (datum->output - input[i]);
        output[i] = datum->output;
    }
    azaError = AZA_SUCCESS;
    return azaError;
}

int azaHighPass(const float *input, float *output, azaHighPassData *data, int frames, int channels) {
    if (input == NULL || output == NULL || data == NULL) {
        azaError = AZA_ERROR_NULL_POINTER;
        return azaError;
    }
    if (channels < 1) {
        azaError = AZA_ERROR_INVALID_CHANNEL_COUNT;
        return azaError;
    }
    if (frames < 1) {
        azaError = AZA_ERROR_INVALID_FRAME_COUNT;
        return azaError;
    }

    for (int i = 0; i < frames*channels; i++) {
        azaHighPassData *datum = &data[i % channels];

        float amount = expf(-8.0f * (datum->frequency / datum->samplerate));
        datum->output = input[i] + amount * (datum->output - input[i]);
        output[i] = input[i] - datum->output;
    }
    azaError = AZA_SUCCESS;
    return azaError;
}

int azaCompressor(const float *input, float *output, azaCompressorData *data, int frames, int channels) {
    if (input == NULL || output == NULL || data == NULL) {
        azaError = AZA_ERROR_NULL_POINTER;
        return azaError;
    }
    if (channels < 1) {
        azaError = AZA_ERROR_INVALID_CHANNEL_COUNT;
        return azaError;
    }
    if (frames < 1) {
        azaError = AZA_ERROR_INVALID_FRAME_COUNT;
        return azaError;
    }

    for (int i = 0; i < frames*channels; i++) {
        azaCompressorData *datum = &data[i % channels];

        float rms;
        azaRms(&input[i], &rms, &datum->rms, 1, 1);
        rms = log2f(rms)*6.0f;
        float t = datum->samplerate / 1000.0f; // millisecond units
        float mult;
        if (datum->ratio > 0.0f) {
            mult = (1.0f - 1.0f / datum->ratio);
        } else {
            mult = -datum->ratio;
        }
        if (rms > datum->attenuation) {
            datum->attenuation = rms + expf(-1.0f / (datum->attack * t)) * (datum->attenuation - rms);
        } else {
            datum->attenuation = rms + expf(-1.0f / (datum->decay * t)) * (datum->attenuation - rms);
        }
        float gain;
        if (datum->attenuation > datum->threshold) {
            gain = mult * (datum->threshold - datum->attenuation);
        } else {
            gain = 0.0f;
        }
        datum->gain = gain;
        output[i] = input[i] * powf(2.0f,gain/6.0f);
    }
    azaError = AZA_SUCCESS;
    return azaError;
}

int azaDelay(const float *input, float *output, azaDelayData *data, int frames, int channels) {
    if (input == NULL || output == NULL || data == NULL) {
        azaError = AZA_ERROR_NULL_POINTER;
        return azaError;
    }
    if (channels < 1) {
        azaError = AZA_ERROR_INVALID_CHANNEL_COUNT;
        return azaError;
    }
    if (frames < 1) {
        azaError = AZA_ERROR_INVALID_FRAME_COUNT;
        return azaError;
    }

    for (int i = 0; i < frames*channels; i++) {
        azaDelayData *datum = &data[i % channels];

        datum->buffer[datum->index] = input[i] + datum->buffer[datum->index] * datum->feedback;
        datum->index++;
        if (datum->index >= datum->samples) {
            datum->index = 0;
        }
        output[i] = datum->buffer[datum->index] * datum->amount + input[i];
    }
    azaError = AZA_SUCCESS;
    return azaError;
}

int azaReverb(const float *input, float *output, azaReverbData *data, int frames, int channels) {
    if (input == NULL || output == NULL || data == NULL) {
        azaError = AZA_ERROR_NULL_POINTER;
        return azaError;
    }
    if (channels < 1) {
        azaError = AZA_ERROR_INVALID_CHANNEL_COUNT;
        return azaError;
    }
    if (frames < 1) {
        azaError = AZA_ERROR_INVALID_FRAME_COUNT;
        return azaError;
    }

    for (int i = 0; i < frames*channels; i++) {
        azaReverbData *datum = &data[i % channels];

        float out = input[i];
        float feedback = 0.98f - (0.2f / datum->roomsize);
        float color = datum->color * 4000.0f;
        for (int ii = 0; ii < AZURE_AUDIO_REVERB_DELAY_COUNT*2/3; ii++) {
            datum->delay[ii].feedback = feedback;
            datum->delay[ii].amount = 1.0f;
            datum->lowPass[ii].samplerate = 44100.0f;
            datum->lowPass[ii].frequency = color;
            float early = input[i];
            azaLowPass(&early, &early, &datum->lowPass[ii], 1, 1);
            azaDelay(&early, &early, &datum->delay[ii], 1, 1);
            out += early - input[i];
        }
        for (int ii = AZURE_AUDIO_REVERB_DELAY_COUNT*2/3; ii < AZURE_AUDIO_REVERB_DELAY_COUNT; ii++) {
            datum->delay[ii].feedback = (float)(ii+8) / (AZURE_AUDIO_REVERB_DELAY_COUNT + 8.0f);
            datum->delay[ii].amount = 1.0f;
            datum->lowPass[ii].samplerate = 44100.0f;
            datum->lowPass[ii].frequency = color*2.0f;
            float diffuse = out/(float)(1+ii);
            azaLowPass(&diffuse, &diffuse, &datum->lowPass[ii], 1, 1);
            azaDelay(&diffuse, &diffuse, &datum->delay[ii], 1, 1);
            out += diffuse - out/(float)(1+ii);
        }
        out *= datum->amount;
        output[i] = out + input[i];
    }
    azaError = AZA_SUCCESS;
    return azaError;
}

int azaSampler(const float *input, float *output, azaSamplerData *data, int frames, int channels) {
    if (output == NULL || data == NULL) {
        azaError = AZA_ERROR_NULL_POINTER;
        return azaError;
    }
    if (channels < 1) {
        azaError = AZA_ERROR_INVALID_CHANNEL_COUNT;
        return azaError;
    }
    if (frames < 1) {
        azaError = AZA_ERROR_INVALID_FRAME_COUNT;
        return azaError;
    }

    for (int i = 0; i < frames*channels; i++) {
        azaSamplerData *datum = &data[i % channels];

        datum->s = datum->speed + expf(-1.0f / (AZURE_AUDIO_SAMPLER_TRANSITION_FRAMES)) * (datum->s - datum->speed);
        datum->g = datum->gain + expf(-1.0f / (AZURE_AUDIO_SAMPLER_TRANSITION_FRAMES)) * (datum->g - datum->gain);


        float sample = 0.0f;

        /* Lanczos
        int t = (int)datum->frame + (int)datum->s;
        for (int i = (int)datum->frame-2; i <= t+2; i++) {
            float x = datum->frame - (float)(i);
            sample += datum->buffer->samples[i % datum->buffer->frames] * sinc(x) * sinc(x/3);
        }
        */

        ///* Cubic
        float abcd[4];
        int ii = (int)datum->frame-2;
        // Oversampling?
        for (int i = 0; i < 4; i++) {
            abcd[i] = datum->buffer->samples[ii++ % datum->buffer->frames];
        }
        sample = cubic(abcd[0], abcd[1], abcd[2], abcd[3], datum->frame - (float)((int)datum->frame));
        //*/

        /* Linear
        int t = (int)datum->frame + (int)datum->s;
        for (int i = (int)datum->frame; i <= t+1; i++) {
            float x = datum->frame - (float)(i);
            sample += datum->buffer->samples[i % datum->buffer->frames] * linc(x);
        }
        */

        output[i] = sample * datum->g;
        datum->frame = datum->frame + datum->s;
        if ((int)datum->frame > datum->buffer->frames) {
            datum->frame -= (float)datum->buffer->frames;
        }
    }
    azaError = AZA_SUCCESS;
    return azaError;
}

static int azaGNumNoInputs = 0;
/* This routine will be called by the PortAudio engine when audio is needed.
** It may be called at interrupt level on some machines so don't do anything
** that could mess up the system like calling malloc() or free().
*/
static int azaPortAudioCallback( const void *inputBuffer, void *outputBuffer,
                         unsigned long framesPerBuffer,
                         const PaStreamCallbackTimeInfo* timeInfo,
                         PaStreamCallbackFlags statusFlags,
                         void *userData )
{
    float *out = (float*)outputBuffer;
    const float *in = (const float*)inputBuffer;
    unsigned int i;
    (void) timeInfo; /* Prevent unused variable warnings. */
    (void) statusFlags;

    //printf("framesPerBuffer = %lu\n",framesPerBuffer);

    /*if (inputBuffer == NULL)
    {
        for(i=0; i<framesPerBuffer; i++)
        {
            *out++ = 0;
            *out++ = 0;
        }
        azaGNumNoInputs += 1;
    }
    else*/
    {
        if (azaMix(in, out, framesPerBuffer, 2, userData)) {
            azaPrint("Error in mix function");
            return paComplete;
        }
    }
    return paContinue;
}

int azaMicTestStart(azaStream *stream) {
    PaStreamParameters inputParameters, outputParameters;
    PaError err;

    azaPrint("Starting mic test");

    err = Pa_Initialize();
    if (err != paNoError) {
        azaPrint("Error: Failed to initialize PortAudio");
        azaError = AZA_ERROR_PORTAUDIO;
        return azaError;
    }


    inputParameters.device = Pa_GetDefaultInputDevice(); /* default input device */
    if (inputParameters.device == paNoDevice) {
        azaPrint("Error: No default input device");
        azaError = AZA_ERROR_PORTAUDIO;
        return azaError;
    }
    azaDefaultMixFuncData.channels = 2;
    if (azaDefaultMixDataInit(&azaDefaultMixFuncData)) {
        return azaError;
    }
    inputParameters.channelCount = 2;
    inputParameters.sampleFormat = paFloat32;
    inputParameters.suggestedLatency = 512.0 / 44100.0;
    //inputParameters.suggestedLatency = Pa_GetDeviceInfo(inputParameters.device)->defaultLowInputLatency;
    inputParameters.hostApiSpecificStreamInfo = NULL;

    outputParameters.device = Pa_GetDefaultOutputDevice(); /* default output device */
    if (outputParameters.device == paNoDevice) {
        azaPrint("Error: No default output device");
        azaError = AZA_ERROR_PORTAUDIO;
        return azaError;
    }
    outputParameters.channelCount = 2;       /* stereo output */
    outputParameters.sampleFormat = paFloat32;
    outputParameters.suggestedLatency = 1024.0 / 44100.0;
    //outputParameters.suggestedLatency = Pa_GetDeviceInfo(outputParameters.device)->defaultLowOutputLatency;
    outputParameters.hostApiSpecificStreamInfo = NULL;

    err = Pa_OpenStream(
            (PaStream**)&stream->stream,
            NULL,
            &outputParameters,
            44100,
            AZURE_AUDIO_FRAMES_PER_BUFFER,
            0, /* paClipOff, */  /* we won't output out of range samples so don't bother clipping them */
            azaPortAudioCallback,
            azaMixData);
    if (err != paNoError) {
        azaPrint("Error: Failed to open PortAudio stream");
        azaError = AZA_ERROR_PORTAUDIO;
        return azaError;
    }
    const PaStreamInfo *streamInfo = Pa_GetStreamInfo((PaStream*)stream->stream);
    double samplerate = streamInfo->sampleRate;
    printf("Stream latency input: %f output: %f samplerate: %f\n",samplerate*streamInfo->inputLatency, samplerate*streamInfo->outputLatency, samplerate);

    err = Pa_StartStream((PaStream*)stream->stream);
    if (err != paNoError) {
        azaPrint("Error: Failed to start PortAudio stream");
        azaError = AZA_ERROR_PORTAUDIO;
        return azaError;
    }
    azaError = AZA_SUCCESS;
    return azaError;
}

int azaMicTestStop(azaStream *stream) {
    PaError err;
    if (stream == NULL) {
        azaPrint("Error: stream is null");
        azaError = AZA_ERROR_NULL_POINTER;
        return azaError;
    }
    err = Pa_CloseStream((PaStream*)stream->stream);
    if (err != paNoError) {
        azaPrint("Error: Failed to close PortAudio stream");
        azaError = AZA_ERROR_PORTAUDIO;
        return azaError;
    }

    Pa_Terminate();
    if (azaDefaultMixDataClean(&azaDefaultMixFuncData))
        return azaError;

    azaError = AZA_SUCCESS;
    return azaError;
}
