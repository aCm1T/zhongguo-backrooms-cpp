#pragma once

#include "sdl_minimal.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

class AudioEngine {
public:
    bool initialize(){
        SDL_AudioSpec wanted{};wanted.freq=48000;wanted.format=AUDIO_F32SYS;wanted.channels=2;wanted.samples=1024;
        device_=SDL_OpenAudioDevice(nullptr,0,&wanted,&obtained_,0);
        if(!device_){std::cerr<<"Audio disabled: "<<SDL_GetError()<<'\n';return false;}
        SDL_PauseAudioDevice(device_,0);return true;
    }
    ~AudioEngine(){shutdown();}
    void shutdown(){if(device_){SDL_ClearQueuedAudio(device_);SDL_CloseAudioDevice(device_);device_=0;}}
    void setVolume(float value){volume_=std::clamp(value,0.f,1.f);}
    bool active()const{return device_!=0;}

    void update(int zone,bool moving,float footGain=1.f){
        if(!device_)return;
        const Uint32 target=static_cast<Uint32>(obtained_.freq*obtained_.channels*sizeof(float)*.18);
        if(SDL_GetQueuedAudioSize(device_)>=target)return;
        constexpr int frames=2048;buffer_.resize(frames*obtained_.channels);
        const float tau=6.28318530718f,dt=1.f/static_cast<float>(obtained_.freq);
        for(int i=0;i<frames;++i){
            phase_+=dt;float noise=randomSigned();noiseSmooth_=noiseSmooth_*.94f+noise*.06f;
            float s=.045f*std::sin(tau*50.f*phase_)+.012f*std::sin(tau*100.f*phase_);
            if(zone==2||zone==8)s+=noise*.055f+noiseSmooth_*.045f; // monsoon / canal rain
            else if(zone==3)s+=.025f*std::sin(tau*121.f*phase_)*(1.f+.3f*std::sin(tau*.7f*phase_));
            else if(zone==7)s+=noiseSmooth_*.13f+.018f*std::sin(tau*72.f*phase_); // winter wind
            else if(zone==4)s+=.012f*std::sin(tau*220.f*phase_)+.007f*std::sin(tau*330.f*phase_);
            else if(zone==6)s+=.014f*std::sin(tau*82.f*phase_)+noiseSmooth_*.025f;
            else if(zone==9)s+=noiseSmooth_*.09f+.016f*std::sin(tau*48.f*phase_)+.008f*std::sin(tau*190.f*phase_); // desert wind + metal
            const float stepPeriod=.46f+(1.f-std::clamp(footGain,0.f,1.f))*.16f;
            if(moving){stepClock_+=dt;if(stepClock_>stepPeriod){stepClock_=0;stepEnvelope_=1.f;}}
            else stepClock_=std::min(stepClock_,.2f);
            if(stepEnvelope_>.001f){s+=(noise*.15f+.08f*std::sin(tau*74.f*phase_))*stepEnvelope_*footGain;stepEnvelope_*=.994f;}
            s=std::tanh(s*1.8f)*volume_;
            for(int c=0;c<obtained_.channels;++c)buffer_[i*obtained_.channels+c]=s*(c==0?.98f:1.02f);
        }
        SDL_QueueAudio(device_,buffer_.data(),static_cast<Uint32>(buffer_.size()*sizeof(float)));
    }
private:
    float randomSigned(){rng_=rng_*1664525u+1013904223u;return static_cast<float>((rng_>>8)&0xFFFFu)/32767.5f-1.f;}
    SDL_AudioDeviceID device_{};SDL_AudioSpec obtained_{};std::vector<float> buffer_;
    std::uint32_t rng_{0x91e10da5u};float volume_{.65f},phase_{},noiseSmooth_{},stepClock_{},stepEnvelope_{};
};
