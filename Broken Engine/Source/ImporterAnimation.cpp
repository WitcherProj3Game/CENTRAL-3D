#include "ImporterAnimation.h"
#include "Application.h"
#include "ModuleFileSystem.h"
#include "ModuleResourceManager.h"

#include "ResourceAnimation.h"

#include "Assimp/include/scene.h"

#include "Math.h"

#include "mmgr/mmgr.h"


using namespace Broken;

ImporterAnimation::ImporterAnimation() : Importer(Importer::ImporterType::Animation)
{
}

ImporterAnimation::~ImporterAnimation()
{
}

Resource* ImporterAnimation::Import(ImportData& IData) const
{
	ImportAnimationData data = (ImportAnimationData&)IData;

	ResourceAnimation* resource_anim = (ResourceAnimation*)App->resources->CreateResource(Resource::ResourceType::ANIMATION, IData.path);

	//Basic animation info
	resource_anim->duration = data.animation->mDuration;
	resource_anim->ticksPerSecond = data.animation->mTicksPerSecond;
	resource_anim->numChannels = data.animation->mNumChannels;
	resource_anim->name = data.animation->mName.C_Str();

	//Creating space for channels
	resource_anim->channels = new Channel[data.animation->mNumChannels];

	//Loading channels info
	for (uint i = 0; i < data.animation->mNumChannels; i++)
	{
		
		LoadChannel(data.animation->mChannels[i], resource_anim->channels[i]);
		
	}

	// --- Save to library ---
	Save(resource_anim);

	App->resources->AddResourceToFolder(resource_anim);


	return resource_anim;
}

void ImporterAnimation::LoadChannel(const aiNodeAnim* AnimNode, Channel& channel)const
{
	for (uint i = 0; i < AnimNode->mNumPositionKeys; i++)
		channel.PositionKeys[AnimNode->mPositionKeys[i].mTime] = float3(AnimNode->mPositionKeys[i].mValue.x, AnimNode->mPositionKeys[i].mValue.y, AnimNode->mPositionKeys[i].mValue.z);

	for (uint i = 0; i < AnimNode->mNumRotationKeys; i++)
		channel.RotationKeys[AnimNode->mRotationKeys[i].mTime] = Quat(AnimNode->mRotationKeys[i].mValue.x, AnimNode->mRotationKeys[i].mValue.y, AnimNode->mRotationKeys[i].mValue.z, AnimNode->mRotationKeys[i].mValue.w);

	for (uint i = 0; i < AnimNode->mNumScalingKeys; i++)
		channel.ScaleKeys[AnimNode->mScalingKeys[i].mTime] = float3(AnimNode->mScalingKeys[i].mValue.x, AnimNode->mScalingKeys[i].mValue.y, AnimNode->mScalingKeys[i].mValue.z);

	channel.name = AnimNode->mNodeName.C_Str();

	//------------------------------- Delete _$AssimpFbx$_ -----------------------
	// Search for the substring in string
	size_t pos = channel.name.find("_$AssimpFbx$_");

	if (pos != std::string::npos)
	{
		std::string ToDelete = "_$AssimpFbx$_";

		// If found then erase it from string
		channel.name.erase(pos, ToDelete.length());
	}

}

void ImporterAnimation::Save(ResourceAnimation* anim) const
{
	//----------------------------- CALCULATE SIZE ----------------------------------

	uint sourcefilename_length = std::string(anim->GetOriginalFile()).size();

	uint length = sourcefilename_length;

	uint anim_name_size = anim->name.size();

	//Animation Duration, TicksperSec, numChannels
	uint size = sizeof(length) + sizeof(const char) * sourcefilename_length + sizeof(anim_name_size) + sizeof(const char)* anim_name_size + sizeof(float) + sizeof(float) + sizeof(uint);

	for (int i = 0; i < anim->numChannels; i++)
	{

		//Size name + Name
		size += sizeof(uint) + anim->channels[i].name.size() + sizeof(uint) * 3;

		//PositionsKeys size
		size += sizeof(double) * anim->channels[i].PositionKeys.size();
		size += sizeof(float) * 3 * anim->channels[i].PositionKeys.size();
		//RotationsKeys size
		size += sizeof(double) * anim->channels[i].RotationKeys.size();
		size += sizeof(float) * 4 * anim->channels[i].RotationKeys.size();
		//ScalesKeys size
		size += sizeof(double) * anim->channels[i].ScaleKeys.size();
		size += sizeof(float) * 3 * anim->channels[i].ScaleKeys.size();
	}
	//-------------------------------------------------------------------------------

	//---------------------------------- Allocate -----------------------------------
	char* data = new char[size];
	char* cursor = data;

	// --- Store name length ---
	memcpy(cursor, &length, sizeof(length));
	cursor += sizeof(length);

	// --- Store original filename ---
	memcpy(cursor, anim->GetOriginalFile(), sizeof(const char) * sourcefilename_length);
	cursor+= sizeof(const char) * sourcefilename_length;

	// --- Store animation name length ---
	memcpy(cursor, &anim_name_size, sizeof(anim_name_size));
	cursor += sizeof(anim_name_size);

	//Name
	memcpy(cursor, anim->name.c_str(), anim->name.size());
	cursor += anim->name.size();

	//Duration
	memcpy(cursor, &anim->duration, sizeof(float));
	cursor += sizeof(float);

	//TicksperSec
	memcpy(cursor, &anim->ticksPerSecond, sizeof(float));
	cursor += sizeof(float);

	//numChannels
	memcpy(cursor, &anim->numChannels, sizeof(uint));
	cursor += sizeof(uint);

	//Channels
	for (int i = 0; i < anim->numChannels; i++)
	{
		//Name size 
		uint SizeofName = anim->channels[i].name.size();
		memcpy(cursor, &SizeofName, sizeof(uint));
		cursor += sizeof(uint);

		//name data
		memcpy(cursor, anim->channels[i].name.c_str(), anim->channels[i].name.size());
		cursor += anim->channels[i].name.size();

		//Poskeys, Rotkeys and Scalekeys SIZES
		uint ranges[3] = { anim->channels[i].PositionKeys.size(), anim->channels[i].RotationKeys.size(), anim->channels[i].ScaleKeys.size() };
		memcpy(cursor, ranges, sizeof(ranges));
		cursor += sizeof(ranges);

		//PositionKeys
		std::map<double, float3>::const_iterator it = anim->channels[i].PositionKeys.begin();
		for (it = anim->channels[i].PositionKeys.begin(); it != anim->channels[i].PositionKeys.end(); it++)
		{
			memcpy(cursor, &it->first, sizeof(double));
			cursor += sizeof(double);

			memcpy(cursor, &it->second, sizeof(float) * 3);
			cursor += sizeof(float) * 3;
		}

		//RotationKeys
		std::map<double, Quat>::const_iterator it2 = anim->channels[i].RotationKeys.begin();
		for (it2 = anim->channels[i].RotationKeys.begin(); it2 != anim->channels[i].RotationKeys.end(); it2++)
		{
			memcpy(cursor, &it2->first, sizeof(double));
			cursor += sizeof(double);

			memcpy(cursor, &it2->second, sizeof(float) * 4);
			cursor += sizeof(float) * 4;
		}

		//ScaleKeys
		std::map<double, float3>::const_iterator it3 = anim->channels[i].ScaleKeys.begin();
		for (it3 = anim->channels[i].ScaleKeys.begin(); it3 != anim->channels[i].ScaleKeys.end(); it3++)
		{
			memcpy(cursor, &it3->first, sizeof(double));
			cursor += sizeof(double);

			memcpy(cursor, &it3->second, sizeof(float) * 3);
			cursor += sizeof(float) * 3;
		}
	}

	App->fs->Save(anim->GetResourceFile(), data, size);


	if (data)
	{
		delete[] data;
		data = nullptr;
		cursor = nullptr;
	}
}

Resource* ImporterAnimation::Load(const char* path) const
{

	Resource* anim = nullptr;
	char* buffer = nullptr;
	
	if (App->fs->Exists(path))
	{
		App->fs->Load(path, &buffer);

		if (buffer)
		{
			// --- Read ranges first ---
			char* cursor = buffer;
			uint ranges;
			uint bytes = sizeof(ranges);
			memcpy(&ranges, cursor, bytes);

			// --- Read the original file's name ---
			std::string source_file;
			source_file.resize(ranges);
			cursor += bytes;
			bytes = sizeof(char) * ranges;
			memcpy((char*)source_file.c_str(), cursor, bytes);

			// --- Extract UID from path ---
			std::string uid = path;
			App->fs->SplitFilePath(path, nullptr, &uid);
			uid = uid.substr(0, uid.find_last_of("."));


			anim = App->resources->animations.find(std::stoi(uid)) != App->resources->animations.end() ? App->resources->animations.find(std::stoi(uid))->second : App->resources->CreateResourceGivenUID(Resource::ResourceType::ANIMATION, source_file.c_str(), std::stoi(uid));

			delete[] buffer;
			buffer = nullptr;
			cursor = nullptr;
		}

	}

	return anim;
}
