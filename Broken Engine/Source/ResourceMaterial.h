#ifndef __RESOURCE_MATERIAL_H__
#define __RESOURCE_MATERIAL_H__

#include "Resource.h"
#include "Math.h"
#include "ModuleRenderer3D.h"

BE_BEGIN_NAMESPACE

class ResourceTexture;
class ResourceShader;
struct Uniform;

class BROKEN_API ResourceMaterial : public Resource 
{
	friend class ComponentMeshRenderer;
	friend class ImporterMaterial;
public:
	ResourceMaterial(uint UID, const char* source_file);
	~ResourceMaterial();

	bool LoadInMemory() override;
	void FreeMemory() override;
	void CreateInspectorNode() override;

	void UpdateUniforms();
	void DisplayAndUpdateUniforms();	

	void SetBlending() const;

private:

	void OnOverwrite() override;
	void OnDelete() override;
	void Repath() override;
	void HandleBlendingSelector(bool& save_material);
	void HandleTextureDisplay(ResourceTexture*& texture, bool& save_material, const char* texture_name, const char* unuse_label, GameObject* container = nullptr);

public:

	bool has_transparencies = false;
	bool has_culling = true;
	float m_Shininess = 1.0f;
	bool m_AffectedBySceneColor = true;
	float4 m_AmbientColor = { 1.0f, 1.0f, 1.0f, 1.0f };
	ResourceTexture* m_DiffuseResTexture = nullptr;
	ResourceTexture* m_SpecularResTexture = nullptr;
	ResourceTexture* m_NormalResTexture = nullptr;

	// Outlining
	bool m_Outline = false;
	bool m_OccludedOutline = false;
	float4 m_OutlineColor = { 0.0f, 0.0f, 0.0f, 0.0f };
	float4 m_OccludedOutlineColor = { 1.0f, 1.0f, 1.0f, 1.0f };
	float m_LineWidth = 100.0f;

	std::string DiffuseResTexturePath;
	std::string SpecularResTexturePath;
	std::string NormalResTexturePath;

	ResourceShader* shader = nullptr;
	std::vector<Uniform*> uniforms;

	std::string previewTexPath;
	bool m_UseTexture = true;
	bool m_ApplyRimLight = false;
	float m_RimPower = 1.0f;
	float2 m_RimSmooth = float2(0.0f, 1.0f);

private:

	bool m_AutoBlending = true;
	BlendAutoFunction m_MatAutoBlendFunc = BlendAutoFunction::STANDARD_INTERPOLATIVE;
	BlendingEquations m_MatBlendEq = BlendingEquations::ADD;
	BlendingTypes m_MatManualBlend_Src = BlendingTypes::SRC_ALPHA, m_MatManualBlend_Dst = BlendingTypes::ONE_MINUS_SRC_ALPHA;
};
BE_END_NAMESPACE
#endif //__RESOURCE_MATERIAL_H__