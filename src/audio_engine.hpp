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

    void playGunshot(bool heavy){pushSfx(heavy?2:1,heavy?1.f:.82f);}
    void playPortal(){pushSfx(3,1.f);}
    void playReload(){pushSfx(4,.7f);}
    void playHit(){pushSfx(5,.85f);}
    void playButton(){pushSfx(6,.9f);}
    void playDoor(){pushSfx(7,.85f);}
    void playSwap(){pushSfx(8,.55f);}
    void playLaser(){pushSfx(9,.8f);}
    void playLand(float hardness){pushSfx(10,std::clamp(hardness,.25f,1.f));}
    void playJumpPad(){pushSfx(11,.85f);}
    void playDryFire(){pushSfx(12,.55f);}
    void playDeny(){pushSfx(13,.5f);}
    void playCube(){pushSfx(14,.6f);}
    void playBreak(){pushSfx(15,.75f);}
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

            for(SfxSlot& slot:slots_){
                if(slot.env<=.001f)continue;
                slot.clock+=dt;
                float burst=0.f;
                const float c=slot.clock;
                if(slot.kind==1){
                    burst=(noise*.55f+.35f*std::sin(tau*920.f*c))*std::exp(-c*28.f);
                }else if(slot.kind==2){
                    burst=(noise*.7f+.25f*std::sin(tau*480.f*c)+.15f*std::sin(tau*180.f*c))*std::exp(-c*18.f);
                }else if(slot.kind==3){
                    burst=.28f*std::sin(tau*(420.f+c*900.f)*c)*std::exp(-c*6.f)+.12f*std::sin(tau*180.f*c);
                }else if(slot.kind==4){
                    burst=.18f*noise*std::exp(-c*14.f)+.08f*std::sin(tau*220.f*c)*std::exp(-c*8.f);
                }else if(slot.kind==5){
                    burst=.22f*std::sin(tau*(760.f-c*200.f)*c)*std::exp(-c*10.f);
                }else if(slot.kind==6){
                    burst=.2f*std::sin(tau*(180.f+c*40.f)*c)*std::exp(-c*7.f)+.1f*noise*std::exp(-c*12.f);
                }else if(slot.kind==7){
                    burst=.25f*std::sin(tau*(70.f+c*30.f)*c)*std::exp(-c*4.5f)+.12f*noiseSmooth_*std::exp(-c*6.f);
                }else if(slot.kind==8){
                    burst=.14f*noise*std::exp(-c*18.f)+.08f*std::sin(tau*320.f*c)*std::exp(-c*14.f);
                }else if(slot.kind==9){
                    burst=.18f*std::sin(tau*(640.f+c*120.f)*c)*std::exp(-c*5.f)+.1f*std::sin(tau*220.f*c);
                }else if(slot.kind==10){
                    burst=(noise*.4f+.15f*std::sin(tau*90.f*c))*slot.env*std::exp(-c*14.f);
                }else if(slot.kind==11){
                    burst=.2f*std::sin(tau*(140.f+c*80.f)*c)*std::exp(-c*5.f);
                }else if(slot.kind==12){ // dry fire
                    burst=.12f*noise*std::exp(-c*22.f)+.06f*std::sin(tau*480.f*c)*std::exp(-c*16.f);
                }else if(slot.kind==13){ // portal deny
                    burst=.12f*std::sin(tau*(220.f-c*80.f)*c)*std::exp(-c*9.f);
                }else if(slot.kind==14){ // cube pick/drop
                    burst=.14f*std::sin(tau*(160.f+c*40.f)*c)*std::exp(-c*8.f)+.05f*noise*std::exp(-c*12.f);
                }else if(slot.kind==15){ // cover break
                    burst=(noise*.5f+.12f*std::sin(tau*110.f*c))*std::exp(-c*11.f);
                }
                s+=burst*slot.env;
                slot.env*=slot.kind==3?.991f:.986f;
            }

            s=std::tanh(s*1.8f)*volume_;
            for(int c=0;c<obtained_.channels;++c)buffer_[i*obtained_.channels+c]=s*(c==0?.98f:1.02f);
        }
        SDL_QueueAudio(device_,buffer_.data(),static_cast<Uint32>(buffer_.size()*sizeof(float)));
    }
private:
    struct SfxSlot{int kind{};float env{};float clock{};};
    void pushSfx(int kind,float env){
        // Prefer a free slot so gunfire and hits can overlap.
        int idx=cursor_;
        for(int i=0;i<3;++i){
            const int j=(cursor_+i)%3;
            if(slots_[j].env<.05f){idx=j;break;}
        }
        slots_[idx]={kind,env,0.f};
        cursor_=(idx+1)%3;
    }
    float randomSigned(){rng_=rng_*1664525u+1013904223u;return static_cast<float>((rng_>>8)&0xFFFFu)/32767.5f-1.f;}
    SDL_AudioDeviceID device_{};SDL_AudioSpec obtained_{};std::vector<float> buffer_;
    std::uint32_t rng_{0x91e10da5u};float volume_{.65f},phase_{},noiseSmooth_{},stepClock_{},stepEnvelope_{};
    SfxSlot slots_[3]{};
    int cursor_{};
    bool laserHum_{};
};
