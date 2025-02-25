#ifndef __IMPORTER_ANIMATION_H__
#define __IMPORTER_ANIMATION_H__

#include "Importer.h"


struct aiAnimation;
struct aiNodeAnim;

BE_BEGIN_NAMESPACE
struct Channel;
class ResourceAnimation;
class Resource;

struct BROKEN_API ImportAnimationData : public Importer::ImportData
{
	ImportAnimationData(const char* path) : Importer::ImportData(path) {};

	aiAnimation* animation = nullptr;
};

class BROKEN_API ImporterAnimation : public Importer
{

public:
	ImporterAnimation();
	virtual ~ImporterAnimation();

	Resource* Import(ImportData& IData) const override;
	void LoadChannel(const aiNodeAnim* AnimNode, Channel& channel)const;
	void Save(ResourceAnimation* anim) const;
	Resource* Load(const char* path) const override;

	static inline Importer::ImporterType GetType() { return Importer::ImporterType::Animation; };
};


BE_END_NAMESPACE
#endif