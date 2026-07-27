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

    void playGunshot(bool heavy){
        sfxKind_=heavy?2:1;
        sfxEnvelope_=heavy?1.f:.82f;
        sfxClock_=0.f;
    }
    void playPortal(){
        sfxKind_=3;
        sfxEnvelope_=1.f;
        sfxClock_=0.f;
    }
    void playReload(){
        sfxKind_=4;
        sfxEnvelope_=.7f;
        sfxClock_=0.f;
    }
    void playHit(){
        sfxKind_=5;
        sfxEnvelope_=.85f;
        sfxClock_=0.f;
    }
    void playButton(){
        sfxKind_=6;
        sfxEnvelope_=.9f;
        sfxClock_=0.f;
    }
    void playDoor(){
        sfxKind_=7;
        sfxEnvelope_=.85f;
        sfxClock_=0.f;
    }
    void playSwap(){
        sfxKind_=8;
        sfxEnvelope_=.55f;
        sfxClock_=0.f;
    }
    void playLaser(){
        sfxKind_=9;
        sfxEnvelope_=.8f;
        sfxClock_=0.f;
    }
    void playLand(float hardness){
        sfxKind_=10;
        sfxEnvelope_=std::clamp(hardness,.25f,1.f);
        sfxClock_=0.f;
    }
    void playJumpPad(){
        sfxKind_=11;
        sfxEnvelope_=.85f;
        sfxClock_=0.f;
    }
    void setLaserHum(bool on){laserHum_=on;}

    void update(int zone,bool moving,float footGain=1.f){
        if(!device_)return;
        const Uint32 target=static_cast<Uint32>(obtained_.freq*obtained_.channels*sizeof(float)*.18);
        if(SDL_GetQueuedAudioSize(device_)>=target)return;
        constexpr int frames=2048;buffer_.resize(frames*obtained_.channels);
        const float tau=6.28318530718f,dt=1.f/static_cast<float>(obtained_.freq);
        for(int i=0;i<frames;++i){
            phase_+=dt;float noise=randomSigned();noiseSmooth_=noiseSmooth_*.94f+noise*.06f;
            float s=.045f*std::sin(tau*50.f*phase_)+.012f*std::sin(tau*100.f*phase_);
            if(zone==2||zone==8)s+=noise*.055f+noiseSmooth_*.045f;
            else if(zone==3)s+=.025f*std::sin(tau*121.f*phase_)*(1.f+.3f*std::sin(tau*.7f*phase_));
            else if(zone==7)s+=noiseSmooth_*.13f+.018f*std::sin(tau*72.f*phase_);
            else if(zone==4)s+=.012f*std::sin(tau*220.f*phase_)+.007f*std::sin(tau*330.f*phase_);
            else if(zone==6)s+=.014f*std::sin(tau*82.f*phase_)+noiseSmooth_*.025f;
            else if(zone==9)s+=noiseSmooth_*.09f+.016f*std::sin(tau*48.f*phase_)+.008f*std::sin(tau*190.f*phase_);
            if(laserHum_){
                s+=.012f*std::sin(tau*240.f*phase_)+.008f*std::sin(tau*480.f*phase_)
                  +.006f*std::sin(tau*90.f*phase_)*(.6f+.4f*std::sin(tau*.7f*phase_));
            }
            const float stepPeriod=.46f+(1.f-std::clamp(footGain,0.f,1.f))*.16f;
            if(moving){stepClock_+=dt;if(stepClock_>stepPeriod){stepClock_=0;stepEnvelope_=1.f;}}
            else stepClock_=std::min(stepClock_,.2f);
            if(stepEnvelope_>.001f){s+=(noise*.15f+.08f*std::sin(tau*74.f*phase_))*stepEnvelope_*footGain;stepEnvelope_*=.994f;}

            if(sfxEnvelope_>.001f){
                sfxClock_+=dt;
                float burst=0.f;
                if(sfxKind_==1){
                    burst=(noise*.55f+.35f*std::sin(tau*920.f*sfxClock_))*std::exp(-sfxClock_*28.f);
                }else if(sfxKind_==2){
                    burst=(noise*.7f+.25f*std::sin(tau*480.f*sfxClock_)+.15f*std::sin(tau*180.f*sfxClock_))*std::exp(-sfxClock_*18.f);
                }else if(sfxKind_==3){
                    burst=.28f*std::sin(tau*(420.f+sfxClock_*900.f)*sfxClock_)*std::exp(-sfxClock_*6.f)
                         +.12f*std::sin(tau*180.f*sfxClock_);
                }else if(sfxKind_==4){
                    burst=.18f*noise*std::exp(-sfxClock_*14.f)+.08f*std::sin(tau*220.f*sfxClock_)*std::exp(-sfxClock_*8.f);
                }else if(sfxKind_==5){
                    burst=.22f*std::sin(tau*(760.f-sfxClock_*200.f)*sfxClock_)*std::exp(-sfxClock_*10.f);
                }else if(sfxKind_==6){
                    burst=.2f*std::sin(tau*(180.f+sfxClock_*40.f)*sfxClock_)*std::exp(-sfxClock_*7.f)
                        +.1f*noise*std::exp(-sfxClock_*12.f);
                }else if(sfxKind_==7){
                    burst=.25f*std::sin(tau*(70.f+sfxClock_*30.f)*sfxClock_)*std::exp(-sfxClock_*4.5f)
                        +.12f*noiseSmooth_*std::exp(-sfxClock_*6.f);
                }else if(sfxKind_==8){
                    burst=.14f*noise*std::exp(-sfxClock_*18.f)+.08f*std::sin(tau*320.f*sfxClock_)*std::exp(-sfxClock_*14.f);
                }else if(sfxKind_==9){
                    burst=.18f*std::sin(tau*(640.f+sfxClock_*120.f)*sfxClock_)*std::exp(-sfxClock_*5.f)
                        +.1f*std::sin(tau*220.f*sfxClock_);
                }else if(sfxKind_==10){
                    burst=(noise*.4f+.15f*std::sin(tau*90.f*sfxClock_))*sfxEnvelope_*std::exp(-sfxClock_*14.f);
                }else if(sfxKind_==11){ // jump pad
                    burst=.2f*std::sin(tau*(140.f+sfxClock_*80.f)*sfxClock_)*std::exp(-sfxClock_*5.f);
                }
                s+=burst*sfxEnvelope_;
                sfxEnvelope_*=sfxKind_==3?.991f:.986f;
            }

            s=std::tanh(s*1.8f)*volume_;
            for(int c=0;c<obtained_.channels;++c)buffer_[i*obtained_.channels+c]=s*(c==0?.98f:1.02f);
        }
        SDL_QueueAudio(device_,buffer_.data(),static_cast<Uint32>(buffer_.size()*sizeof(float)));
    }
private:
    float randomSigned(){rng_=rng_*1664525u+1013904223u;return static_cast<float>((rng_>>8)&0xFFFFu)/32767.5f-1.f;}
    SDL_AudioDeviceID device_{};SDL_AudioSpec obtained_{};std::vector<float> buffer_;
    std::uint32_t rng_{0x91e10da5u};float volume_{.65f},phase_{},noiseSmooth_{},stepClock_{},stepEnvelope_{};
    int sfxKind_{};float sfxEnvelope_{},sfxClock_{};
    bool laserHum_{};
};
