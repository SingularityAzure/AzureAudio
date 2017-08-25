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

int azaError;

fpLogCallback azaPrint;

int azaInit() {
    azaPrint = azaDefaultLogFunc;
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

int azaSetLogCallback(fpLogCallback newLogFunc) {
    if (newLogFunc != NULL) {
        azaPrint = newLogFunc;
        azaError = AZA_SUCCESS;
    } else {
        azaError = AZA_ERROR_NULL_POINTER;
    }
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
    data->gain = 0.0f;
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

void azaDelayDataInit(azaDelayData *data, int samples) {
    data->buffer = (float*)malloc(sizeof(float) * samples);
    for (int i = 0; i < samples; i++) {
        data->buffer[i] = 0.0f;
    }
    data->index = 0;
    data->samples = samples;
}

void azaDelayDataClean(azaDelayData *data) {
    free(data->buffer);
}

void azaReverbDataInit(azaReverbData *data, int samples[AZURE_AUDIO_REVERB_DELAY_COUNT]) {
    for (int i = 0; i < AZURE_AUDIO_REVERB_DELAY_COUNT; i++) {
        azaDelayDataInit(&data->delay[i], samples[i]);
        azaLowPassDataInit(&data->lowPass[i]);
    }
}

void azaReverbDataClean(azaReverbData *data) {
    for (int i = 0; i < AZURE_AUDIO_REVERB_DELAY_COUNT; i++) {
        azaDelayDataClean(&data->delay[i]);
    }
}

void azaMixDataInit(azaMixData *data) {
    for (int i = 0; i < 2; i++) {
        data->highPassData[i].samplerate = 44100.0f;
        data->highPassData[i].frequency = 40.0f;
        data->delayData[i].amount = 0.2f;
        data->delayData[i].feedback = 0.2f;
        data->reverbData[i].amount = 0.1f;
        data->reverbData[i].roomsize = 10.0f;
        data->reverbData[i].color = 0.2f;
        data->compressorData[i].samplerate = 44100.0f;
        data->compressorData[i].threshold = -18.0f;
        data->compressorData[i].ratio = 2.0f;
        data->compressorData[i].attack = 50.0f;
        data->compressorData[i].decay = 200.0f;
        data->limiterData[i].gain = 6.0f;
    }
    azaLookaheadLimiterDataInit(&data->limiterData[0]);
    azaLookaheadLimiterDataInit(&data->limiterData[1]);
    azaCompressorDataInit(&data->compressorData[0]);
    azaCompressorDataInit(&data->compressorData[1]);
    azaDelayDataInit(&data->delayData[0], 40000);
    azaDelayDataInit(&data->delayData[1], 30000);
    //int samples[AZURE_AUDIO_REVERB_DELAY_COUNT] = {225, 556, 441, 341};
    int samples[AZURE_AUDIO_REVERB_DELAY_COUNT] = {1557, 1617, 1491, 1422, 1277, 1356, 1188, 1116, 2111, 2133, 225, 556, 441, 341, 713};
    int samplesL[AZURE_AUDIO_REVERB_DELAY_COUNT];
    int samplesR[AZURE_AUDIO_REVERB_DELAY_COUNT];
    for (int i = 0; i < AZURE_AUDIO_REVERB_DELAY_COUNT; i++) {
        samplesL[i] = samples[i];
        samplesR[i] = samples[i] + 23;
    }
    azaReverbDataInit(&data->reverbData[0], samplesL);
    azaReverbDataInit(&data->reverbData[1], samplesR);
    azaHighPassDataInit(&data->highPassData[0]);
    azaHighPassDataInit(&data->highPassData[1]);
}

void azaMixDataClean(azaMixData *data) {
    azaDelayDataClean(&data->delayData[0]);
    azaDelayDataClean(&data->delayData[1]);
    azaReverbDataClean(&data->reverbData[0]);
    azaReverbDataClean(&data->reverbData[1]);
}

float azaCubicLimiter(float input) {
    float output;
    if (input > 1.0f)
        output = 1.0f;
    else if (input < -1.0f)
        output = -1.0f;
    else
        output = input;
    output = 1.5 * output - 0.5f * output * output * output;
    return output;
}

float azaRms(float input, azaRmsData *data) {
    // Buffer for root mean square detection
    data->squared -= data->buffer[data->index];
    data->buffer[data->index] = input * input;
    data->squared += data->buffer[data->index++];
    if (data->index >= AZURE_AUDIO_RMS_SAMPLES)
        data->index = 0;
    return sqrtf(data->squared/AZURE_AUDIO_RMS_SAMPLES);
}

float azaLookaheadLimiter(float input, azaLookaheadLimiterData *data) {
    float peak = input;
    float gain = data->gain;
    if (peak < 0.0f)
        peak = -peak;
    peak = log2f(peak)*6.0f + gain;
    if (peak < 0.0f)
        peak = 0.0f;
    data->sum += peak - data->gainBuffer[data->index];
    float average = data->sum / AZURE_AUDIO_LOOKAHEAD_SAMPLES;
    if (average > peak) {
        data->sum += average - peak;
        peak = average;
    }
    data->gainBuffer[data->index] = peak;

    data->valBuffer[data->index] = input;

    data->index = (data->index+1)%AZURE_AUDIO_LOOKAHEAD_SAMPLES;

    if (average > data->gainBuffer[data->index])
        gain -= average;
    else
        gain -= data->gainBuffer[data->index];
    float output = data->valBuffer[data->index] * powf(2.0f,gain/6.0f);
    if (output < 1.0f && output > -1.0f)
        return output;
    else if (output > 0)
        return 1.0f;
    else
        return -1.0f;
}

float azaLowPass(float input, azaLowPassData *data) {
    float amount = expf(-1.0f * (data->frequency / data->samplerate));
    data->output = input + amount * (data->output - input);
    return data->output;
}

float azaHighPass(float input, azaHighPassData *data) {
    float amount = expf(-1.0f * (data->frequency / data->samplerate));
    data->output = input + amount * (data->output - input);
    return input - data->output;
}

float azaCompressor(float input, azaCompressorData *data) {
    float rms = log2f(azaRms(input, &data->rms))*6.0f;
    float t = data->samplerate / 1000.0f; // millisecond units
    float mult;
    if (data->ratio > 0.0f) {
        mult = (1.0f - 1.0f / data->ratio);
    } else {
        mult = -data->ratio;
    }
    if (rms > data->attenuation) {
        data->attenuation = rms + expf(-1.0f / (data->attack * t)) * (data->attenuation - rms);
    } else {
        data->attenuation = rms + expf(-1.0f / (data->decay * t)) * (data->attenuation - rms);
    }
    float gain;
    if (data->attenuation > data->threshold) {
        gain = mult * (data->threshold - data->attenuation);
    } else {
        gain = 0.0f;
    }
    data->gain = gain;
    return input * powf(2.0f,gain/6.0f);
}

float azaDelay(float input, azaDelayData *data) {
    data->buffer[data->index] = input + data->buffer[data->index] * data->feedback;
    data->index++;
    if (data->index >= data->samples) {
        data->index = 0;
    }
    return data->buffer[data->index] * data->amount + input;
}

float azaReverb(float input, azaReverbData *data) {
    float output = input;
    float feedback = 0.98f - (0.2f / data->roomsize);
    float color = data->color * 4000.0f;
    for (int i = 0; i < AZURE_AUDIO_REVERB_DELAY_COUNT*2/3; i++) {
        data->delay[i].feedback = feedback;
        data->delay[i].amount = 1.0f;
        data->lowPass[i].samplerate = 44100.0f;
        data->lowPass[i].frequency = color;
        output +=
            azaDelay(
                azaLowPass(
                    input,
                &data->lowPass[i]),
            &data->delay[i]) - input;
    }
    for (int i = AZURE_AUDIO_REVERB_DELAY_COUNT*2/3; i < AZURE_AUDIO_REVERB_DELAY_COUNT; i++) {
        data->delay[i].feedback = (float)(i+8) / (AZURE_AUDIO_REVERB_DELAY_COUNT + 8.0f);
        data->delay[i].amount = 1.0f;
        data->lowPass[i].samplerate = 44100.0f;
        data->lowPass[i].frequency = color*2.0f;
        output +=
            azaDelay(
                azaLowPass(
                    output/(float)(1+i),
                &data->lowPass[i]),
            &data->delay[i]) - output/(float)(1+i);
    }
    output *= data->amount;
    return output + input;
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
    azaMixData *mixData = (azaMixData*)userData;

    if (inputBuffer == NULL)
    {
        for(i=0; i<framesPerBuffer; i++)
        {
            *out++ = 0;  /* left - silent */
            *out++ = 0;  /* right - silent */
        }
        azaGNumNoInputs += 1;
    }
    else
    {
        float clip = 0.0f;
        for (i=0; i<framesPerBuffer*2; i++)
        {
            *out = *in * 16.0f;
            *out = azaHighPass(*out, &mixData->highPassData[i%2]);
            *out = azaDelay(*out, &mixData->delayData[i%2]);
            *out = azaReverb(*out, &mixData->reverbData[i%2]);
            *out = azaCompressor(*out, &mixData->compressorData[i%2]);
            *out = azaLookaheadLimiter(*out, &mixData->limiterData[i%2]);

            if (*out > clip) {
                clip = *out;
            }
            out++;
            if (i%2)
                in++;
        }
        if (clip > 1.0f)
            printf("clipped: %f\n", clip);
    }
    //printf("compressor gain = %f\n", mixData->compressorData[0].gain);
    return paContinue;
}

int azaMicTestStart(azaStream *stream, azaMixData *data) {
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
    inputParameters.channelCount = 1;       /* mono input */
    inputParameters.sampleFormat = paFloat32;
    inputParameters.suggestedLatency = 2000.0 / 44100.0;
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
    outputParameters.suggestedLatency = 2000.0 / 44100.0;
    //outputParameters.suggestedLatency = Pa_GetDeviceInfo(outputParameters.device)->defaultLowOutputLatency;
    outputParameters.hostApiSpecificStreamInfo = NULL;

    err = Pa_OpenStream(
            (PaStream**)&stream->stream,
            &inputParameters,
            &outputParameters,
            44100,
            64,
            0, /* paClipOff, */  /* we won't output out of range samples so don't bother clipping them */
            azaPortAudioCallback,
            (void*)data);
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

int azaMicTestStop(azaStream *stream, azaMixData *data) {
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
    azaError = AZA_SUCCESS;
    return azaError;
}
