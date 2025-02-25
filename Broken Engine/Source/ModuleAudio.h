#ifndef __ModuleAudio__H__
#define __ModuleAudio__H__

#include "Module.h"
#include "Math.h"
#include "Wwise/AK/SoundEngine/Common/AkTypes.h"
#include <vector>

BE_BEGIN_NAMESPACE
class BROKEN_API WwiseGameObject
{
public:
	WwiseGameObject(uint64 id, const char* name);
	~WwiseGameObject();

public:
	void SetPosition(float posX = 0, float posY = 0, float posZ = 0, float frontX = 1, float frontY = 0, float frontZ = 0, float topX = 0, float topY = 1, float topZ = 0);
	void PlayEvent(uint id);
	void PauseEvent(uint id);
	void ResumeEvent(uint id);
	void StopEvent(uint id);
	void SetVolume(uint id, float volume);
	WwiseGameObject* CreateAudioSource(uint id, const char* name, float3 position);
	WwiseGameObject* CreateAudioListener(uint id, const char* name, float3 position);
	void SetAuxSends();
	void SetAudioSwitch(std::string SwitchGroup, std::string Switchstate, uint wwiseGOID);
	void SetAudioTrigger(uint wwisegoId, std::string trigger);
	void SetAudioRTPCValue(std::string RTPCName, int value, uint wwiseGOID);

public:
	uint GetID();

public:
	float volume = 1.0f;
	AkGameObjectID id = 0;
	std::string name;

private:
	AkVector position = { 0,0,0 };
	AkVector orientationFront = { 0,0,0 };
	AkVector orientationTop = { 0,0,0 };
};

class BROKEN_API ModuleAudio : public Module
{
	friend class ScriptingScenes;
public:
	ModuleAudio(bool start_enabled = true);
	virtual ~ModuleAudio();

	bool Init(json& file);
	bool Start();
	update_status PostUpdate(float dt);
	bool CleanUp();
	void SetAudioState(std::string StateGroup, std::string State);

private:
	void InitWwise();
	void TerminateWwise();
	void LoadSoundBank(const char* path);
	void CatchAllSoundBanks(std::vector<std::string> *banks);
	void LoadEventsFromJson();
	void StopAllAudioEvents();
	void ResumeAllAudioEvents();
	void PauseAllAudioEvents();	

public:
	AkGameObjectID currentListenerID = 0;
	std::map <std::string, uint> EventMap;
	std::vector<WwiseGameObject*> audioListenerList;
	std::vector<WwiseGameObject*> audioSourceList;
};
BE_END_NAMESPACE
#endif __ModuleAudio__H__