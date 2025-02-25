#include "ComponentMesh.h"

// -- Modules --
#include "Application.h"
#include "ModuleResourceManager.h"
#include "ModuleFileSystem.h"
#include "ModuleRenderer3D.h"
#include "ModuleSceneManager.h"

// -- Components --
#include "GameObject.h"
#include "ComponentBone.h"
#include "ComponentTransform.h"

// -- Resource --
#include "ResourceMesh.h"
#include "ResourceBone.h"

#include "Math.h"
#include "Imgui/imgui.h"
#include "mmgr/mmgr.h"

using namespace Broken;
ComponentMesh::ComponentMesh(GameObject* ContainerGO) : Component(ContainerGO, Component::ComponentType::Mesh) 
{
	name = "Mesh";
}

ComponentMesh::~ComponentMesh() 
{
	if (resource_mesh && resource_mesh->IsInMemory() && resource_mesh->GetUID() != App->scene_manager->plane->GetUID()) 
	{
		resource_mesh->RemoveUser(GO);
		resource_mesh->Release();
	}

	if (deformable_mesh)
	{

		if (deformable_mesh->vertices) {
			delete[] deformable_mesh->vertices;
			deformable_mesh->vertices = nullptr;
		}

		if (deformable_mesh->Indices) {
			delete[] deformable_mesh->Indices;
			deformable_mesh->Indices = nullptr;
		}

		deformable_mesh->VBO = 0;
		deformable_mesh->EBO = 0;
		deformable_mesh->VAO = 0;

		delete deformable_mesh;
		deformable_mesh = nullptr;

	}
	
}

void ComponentMesh::Update()
{
	if (to_delete)
		this->GetContainerGameObject()->RemoveComponent(this);
}

const AABB& ComponentMesh::GetAABB() const
{
	if (deformable_mesh) {
		if (!dmesh_aabb_updated) {
			deformable_mesh->CreateAABB();
			dmesh_aabb_updated = true;
		}
		return deformable_mesh->aabb;
	}
	else if (resource_mesh)
		return resource_mesh->aabb;
	else
		return AABB(float3(-0.5,-0.5,-0.5), float3(0.5, 0.5, 0.5));
}

json ComponentMesh::Save() const
{
	json node;
	node["Active"] = this->active;

	node["Resources"]["ResourceMesh"]["path"];

	// --- Store path to component file ---
	if (resource_mesh)
	{
		node["Resources"]["ResourceMesh"]["PreviewTexture"] = resource_mesh->previewTexPath; // for now we save this here 
		node["Resources"]["ResourceMesh"]["path"] = std::string(resource_mesh->GetResourceFile());
	}

	return node;
}

void ComponentMesh::Load(json& node)
{
	this->active = node.contains("Active") ? (bool)node["Active"] : true;

	std::string path = node["Resources"]["ResourceMesh"]["path"].is_null() ?  "-1" : node["Resources"]["ResourceMesh"]["path"];
	App->fs->SplitFilePath(path.c_str(), nullptr, &path);
	path = path.substr(0, path.find_last_of("."));

	if (resource_mesh)
		resource_mesh->Release();

	if (std::stoi(path) != -1)
		resource_mesh = (ResourceMesh*)App->resources->GetResource(std::stoi(path));

	// --- We want to be notified of any resource event ---
	if (resource_mesh && resource_mesh->GetUID() != App->scene_manager->plane->GetUID())
		resource_mesh->AddUser(GO);
}

void ComponentMesh::ONResourceEvent(uint UID, Resource::ResourceNotificationType type) 
{
	// --- Always check if your resources are already invalidated, since go sends events from all of its components resources ---
	switch (type) 
	{
	case Resource::ResourceNotificationType::Overwrite:
		if (resource_mesh && UID == resource_mesh->GetUID())
			resource_mesh = (ResourceMesh*)App->resources->GetResource(UID);
		break;

	case Resource::ResourceNotificationType::Deletion:
		if (resource_mesh && UID == resource_mesh->GetUID())
			resource_mesh = nullptr;
		break;

	default:
		break;
	}
}

void ComponentMesh::CreateInspectorNode() 
{

	if (resource_mesh) 
	{
		std::string Triangle_count = "Triangles   ";
		Triangle_count.append(std::to_string(resource_mesh->IndicesSize / 3));
		ImGui::Text(Triangle_count.data());

		//ImGui::Text("Instances:");
		//ImGui::SameLine();
		//ImGui::Text(std::to_string(mesh->resource_mesh->instances).data());
	}

	ImGui::SetCursorPosX(ImGui::GetWindowContentRegionWidth() / 2 - 100);
	
}

void ComponentMesh::AddBone(ComponentBone* bone)
{
	for (int i = 0; i < bones.size(); i++)
		if (bones[i] == bone)
			return;

	bones.push_back(bone);
}

void ComponentMesh::UpdateDefMesh()
{
	dmesh_aabb_updated = false;

	if (!deformable_mesh)
	{
		deformable_mesh = new ResourceMesh(App->GetRandom().Int(), "");


		deformable_mesh->VerticesSize = resource_mesh->VerticesSize;
		deformable_mesh->vertices = new Vertex[deformable_mesh->VerticesSize];
		memcpy(deformable_mesh->vertices, resource_mesh->vertices, deformable_mesh->VerticesSize * sizeof(Vertex));

		deformable_mesh->IndicesSize = resource_mesh->IndicesSize;
		deformable_mesh->Indices = new uint[deformable_mesh->IndicesSize];
		memcpy(deformable_mesh->Indices, resource_mesh->Indices, deformable_mesh->IndicesSize * sizeof(uint));

		deformable_mesh->LoadInMemory();
	}
	

	for (uint i = 0; i < deformable_mesh->VerticesSize; ++i)
	{
		deformable_mesh->vertices[i].position[0] = 0.0f;
		deformable_mesh->vertices[i].position[1] = 0.0f;
		deformable_mesh->vertices[i].position[2] = 0.0f;

		deformable_mesh->vertices[i].normal[0] = 0.0f;
		deformable_mesh->vertices[i].normal[1] = 0.0f;
		deformable_mesh->vertices[i].normal[2] = 0.0f;
	}

	for (std::vector<ComponentBone*>::iterator it = bones.begin(); it != bones.end(); ++it)
	{
		ResourceBone* r_bone = (*it)->res_bone;

		float4x4 mat = (*it)->GetSkeletonTransform();
		mat = GO->GetComponent<ComponentTransform>()->GetLocalTransform().Inverted() * mat;
		mat = mat * r_bone->matrix;

		for (uint i = 0; i < r_bone->NumWeights; i++)
		{

			uint index = r_bone->index_weight[i];

			// -- This is the original vertex
			float3 tmp = { resource_mesh->vertices[index].position[0],
						  resource_mesh->vertices[index].position[1],
						  resource_mesh->vertices[index].position[2]};
					   			 


			float3 _vertex = mat.TransformPos(tmp.x, tmp.y, tmp.z);//LINE OF DEATH

			deformable_mesh->vertices[index].position[0] += _vertex.x * r_bone->weight[i];
			deformable_mesh->vertices[index].position[1] += _vertex.y * r_bone->weight[i];
			deformable_mesh->vertices[index].position[2] += _vertex.z * r_bone->weight[i];


			_vertex = mat.TransformPos(deformable_mesh->vertices[index].normal[0],
										deformable_mesh->vertices[index].normal[1], 
										deformable_mesh->vertices[index].normal[2]);

			deformable_mesh->vertices[index].normal[0] += _vertex.x * r_bone->weight[i];
			deformable_mesh->vertices[index].normal[1] += _vertex.y * r_bone->weight[i];
			deformable_mesh->vertices[index].normal[2] += _vertex.z * r_bone->weight[i];

			float3 normal = float3(deformable_mesh->vertices[index].normal[0], deformable_mesh->vertices[index].normal[1], deformable_mesh->vertices[index].normal[2]);
			normal.Normalize();

			deformable_mesh->vertices[index].normal[0] = normal.x;
			deformable_mesh->vertices[index].normal[1] = normal.y;
			deformable_mesh->vertices[index].normal[2] = normal.z;

		}
	}
	// --- Bind it ---
	glBindVertexArray(deformable_mesh->VAO);

	// Bind the VBO 
	glBindBuffer(GL_ARRAY_BUFFER, deformable_mesh->VBO);

	// --- Set all vertex attribute pointers ---

	glBufferData(GL_ARRAY_BUFFER, sizeof(Vertex) * deformable_mesh->VerticesSize, deformable_mesh->vertices, GL_DYNAMIC_DRAW);

	// --- Unbind VAO and VBO ---
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}
