#include "ScriptingTransform.h"

// -- Modules --
#include "Application.h"
#include "ModuleScripting.h"
#include "ModuleSceneManager.h"

// -- Components -
#include "GameObject.h"
#include "Components.h"
#include "ComponentTransform.h"

#include "ScriptData.h"
#include "ResourceScene.h"

// -- Utilities --
#include "TranslatorUtilities.h"

using namespace Broken;
ScriptingTransform::ScriptingTransform() {}

ScriptingTransform::~ScriptingTransform() {}

luabridge::LuaRef ScriptingTransform::GetPosition(uint gameobject_UUID, lua_State* L)
{
	float3 pos = float3(0.0f);
	GameObject* go = App->scene_manager->currentScene->GetGOWithUID(gameobject_UUID);

	if (go) {
		ComponentTransform* transform = go->GetComponent<ComponentTransform>();

		if (transform)
		{
			pos = transform->GetGlobalPosition();
		}
		else
			ENGINE_CONSOLE_LOG("Object or its transformation component are null");
	}
	else
		ENGINE_CONSOLE_LOG("(SCRIPTING) Alert! Could not find GameObject with UUID %d", gameobject_UUID);

	luabridge::LuaRef table = luabridge::newTable(L);
	table.append(pos.x);
	table.append(pos.y);
	table.append(pos.z);

	//This is an example on how to use the new lua customizable tables
	/*luabridge::LuaRef ScriptGetTable = luabridge::getGlobal(L, "NewVector3");
	luabridge::LuaRef table(ScriptGetTable());

	CppLuaTranslatorUtilities translator;
	CppTable vec = translator.GetCppTableFromVec3(pos);

	translator.PassCppTableValuesToLuaTable(vec, table);*/

	return table;
}

void ScriptingTransform::Translate(float x, float y, float z, uint gameobject_UUID)
{
	GameObject* go = App->scene_manager->currentScene->GetGOWithUID(gameobject_UUID);

	if (go) {
		ComponentTransform* transform = go->GetComponent<ComponentTransform>();

		if (transform)
		{
			float3 trans_pos = transform->GetPosition();

			trans_pos.x += x;
			trans_pos.y += y;
			trans_pos.z += z;

			transform->SetPosition(trans_pos.x, trans_pos.y, trans_pos.z);
		}
		else
			ENGINE_CONSOLE_LOG("Object or its transformation component are null");
	}
	else
		ENGINE_CONSOLE_LOG("(SCRIPTING) Alert! Could not find GameObject with UUID %d", gameobject_UUID);
}

void ScriptingTransform::SetPosition(float x, float y, float z, uint gameobject_UUID)
{
	GameObject* go = App->scene_manager->currentScene->GetGOWithUID(gameobject_UUID);

	if (go) {
		ComponentTransform* transform = go->GetComponent<ComponentTransform>();

		if (transform) {
			transform->SetGlobalPosition(x, y, z);
			go->TransformGlobal();
		}
		else
			ENGINE_CONSOLE_LOG("![Script]: (SetPosition) GOs transform component is null");
	}
	else
		ENGINE_CONSOLE_LOG("![Script]: (SetPosition) Could not find GameObject with UUID %d", gameobject_UUID);
}

void ScriptingTransform::SetLocalPosition(float x, float y, float z, uint gameobject_UUID) {
	GameObject* go = App->scene_manager->currentScene->GetGOWithUID(gameobject_UUID);

	if (go) {
		ComponentTransform* transform = go->GetComponent<ComponentTransform>();

		if (transform) {
			transform->SetPosition(x, y, z);
			go->TransformGlobal();
		}
		else
			ENGINE_CONSOLE_LOG("![Script]: (SetLocalPosition) GOs transform component is null");
	}
	else
		ENGINE_CONSOLE_LOG("![Script]: (SetLocalPosition) Could not find GameObject with UUID %d", gameobject_UUID);
}

void ScriptingTransform::GetScale(uint gameobject_UUID, lua_State* L) {
	GameObject* go = App->scene_manager->currentScene->GetGOWithUID(gameobject_UUID);
	float3 scale = float3::zero;
	if (go) {
		ComponentTransform* transform = go->GetComponent<ComponentTransform>();

		if (transform)
			scale = transform->GetScale();
		else
			ENGINE_CONSOLE_LOG("![Script]: (GetScale) GameObject has no component transform.");
	}
	else
		ENGINE_CONSOLE_LOG("![Script]: (GetScale) Could not find GameObject with UUID %d", gameobject_UUID);

	luabridge::LuaRef table = luabridge::newTable(L);
	table.append(scale.x);
	table.append(scale.y);
	table.append(scale.z);
}

void ScriptingTransform::SetScale(float x, float y, float z, uint gameobject_UUID) {
	GameObject* go = App->scene_manager->currentScene->GetGOWithUID(gameobject_UUID);

	if (go) {
		ComponentTransform* transform = go->GetComponent<ComponentTransform>();

		if (transform)
			transform->Scale(x, y, z);
		else
			ENGINE_CONSOLE_LOG("![Script]: (SetScale) GameObject has no component transform.");
	}
	else
		ENGINE_CONSOLE_LOG("![Script]: (SetScale) Could not find GameObject with UUID %d", gameobject_UUID);
}

void ScriptingTransform::RotateObject(float x, float y, float z, uint gameobject_UUID)
{
	GameObject* go = App->scene_manager->currentScene->GetGOWithUID(gameobject_UUID);

	if (go) {
		ComponentTransform* transform = go->GetComponent<ComponentTransform>();
		ComponentCollider* collider = go->GetComponent<ComponentCollider>();
		ComponentDynamicRigidBody* rb = go->GetComponent<ComponentDynamicRigidBody>();

		if (transform && rb && rb->rigidBody && collider)
		{
			// We get current rotation in quaternions
			float3 rot = transform->GetRotation();
			Quat current_quat = Quat::FromEulerXYZ(DEGTORAD * rot.x, DEGTORAD * rot.y, DEGTORAD * rot.z);

			// We calculate the quaternion of the new rotation and add them up
			Quat quaternion = Quat::FromEulerXYZ(DEGTORAD * x, DEGTORAD * y, DEGTORAD * z);
			quaternion = quaternion * current_quat;

			physx::PxQuat quat = physx::PxQuat(quaternion.x, quaternion.y, quaternion.z, quaternion.w);
		
			physx::PxTransform globalPos = rb->rigidBody->getGlobalPose();
			globalPos = physx::PxTransform(globalPos.p, quat);
			collider->UpdateTransformByRigidBody(rb, transform, &globalPos);

		}
		else if (transform)
		{
			float3 rot = transform->GetRotation();
			rot += float3(x, y, z);
			transform->SetRotation(rot);
			go->TransformGlobal();
		}
		else
			ENGINE_CONSOLE_LOG("![Script]: (RotateObject) Object's transformation component is null");
	}
	else
		ENGINE_CONSOLE_LOG("![Script]: (RotateObject) Could not find GameObject with UUID %d", gameobject_UUID);
}

void ScriptingTransform::SetObjectRotation(float x, float y, float z, uint gameobject_UUID)
{
	GameObject* go = App->scene_manager->currentScene->GetGOWithUID(gameobject_UUID);

	if (go) {
		ComponentTransform* transform = go->GetComponent<ComponentTransform>();
		ComponentCollider* collider = go->GetComponent<ComponentCollider>();
		ComponentDynamicRigidBody* rb = go->GetComponent<ComponentDynamicRigidBody>();

		if (transform && rb && rb->rigidBody && collider)
		{
			// We calculate the quaternion of the new rotation
			Quat quaternion = Quat::FromEulerXYZ(DEGTORAD * x, DEGTORAD * y, DEGTORAD * z);
			physx::PxQuat quat = physx::PxQuat(quaternion.x, quaternion.y, quaternion.z, quaternion.w);
		
			physx::PxTransform globalPos = rb->rigidBody->getGlobalPose();
			globalPos = physx::PxTransform(globalPos.p, quat);
			collider->UpdateTransformByRigidBody(rb, transform, &globalPos);

		}
		else if (transform)
		{
			transform->SetRotation({ x, y, z });
			go->TransformGlobal();
		}
		else
			ENGINE_CONSOLE_LOG("![Script]: (SetObjectRotation) Object's transformation component is null");
	}
	else
		ENGINE_CONSOLE_LOG("![Script]: (SetObjectRotation) Could not find GameObject with UUID %d", gameobject_UUID);
}

void ScriptingTransform::LookAt(float spotX, float spotY, float spotZ, uint gameobject_UUID)
{
	GameObject* go = App->scene_manager->currentScene->GetGOWithUID(gameobject_UUID);

	if (go) {
		ComponentTransform* transform = go->GetComponent<ComponentTransform>();
		ComponentCollider* collider = go->GetComponent<ComponentCollider>();
		ComponentDynamicRigidBody* rb = go->GetComponent<ComponentDynamicRigidBody>();

		if (transform)
		{
			float3 zaxis = float3(transform->GetGlobalPosition() - float3(spotX, spotY, spotZ)).Normalized();
			float3 xaxis = float3(zaxis.Cross(float3(0, 1, 0))).Normalized();
			float3 yaxis = xaxis.Cross(zaxis);
			zaxis = zaxis.Neg();

			float4x4 m = {
			   float4(xaxis.x, xaxis.y, xaxis.z, -Dot(xaxis, transform->GetGlobalPosition())),
			   float4(yaxis.x, yaxis.y, yaxis.z, -Dot(yaxis, transform->GetGlobalPosition())),
			   float4(zaxis.x, zaxis.y, zaxis.z, -Dot(zaxis, transform->GetGlobalPosition())),
			   float4(0, 0, 0, 1)
			};
			m.Transpose();

			float3 pos, scale;
			Quat rot;

			m.Decompose(pos, rot, scale);
			rot = rot.Inverted();

			if (rb && rb->rigidBody && collider)
			{
				physx::PxTransform globalPos = rb->rigidBody->getGlobalPose();
				physx::PxQuat quat = physx::PxQuat(rot.x, rot.y, rot.z, rot.w);
				globalPos = physx::PxTransform(globalPos.p, quat);

				collider->UpdateTransformByRigidBody(rb, transform, &globalPos);
			}
			else
				transform->SetRotation(rot);
		}
		else
			ENGINE_CONSOLE_LOG("Object or its transformation component are null");
	}
	else
		ENGINE_CONSOLE_LOG("(SCRIPTING) Alert! Could not find GameObject with UUID %d", gameobject_UUID);
}

luabridge::LuaRef ScriptingTransform::GetRotation(uint gameobject_UUID, lua_State* L)
{
	float3 rot = float3(0.0f);	
	GameObject* go = App->scene_manager->currentScene->GetGOWithUID(gameobject_UUID);

	if (go) {
		ComponentTransform* transform = go->GetComponent<ComponentTransform>();

		if (transform)
			rot = transform->GetRotation();
		else
			ENGINE_CONSOLE_LOG("Object or its transformation component are null");
	}
	else
		ENGINE_CONSOLE_LOG("(SCRIPTING) Alert! Could not find GameObject with UUID %d", gameobject_UUID);

	luabridge::LuaRef table = luabridge::newTable(L);
	table.append(rot.x);
	table.append(rot.y);
	table.append(rot.z);

	return table;
}