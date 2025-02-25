#include "Application.h"
#include "Imgui/imgui.h"
#include "GameObject.h"
#include "Timer.h"
#include "RandomGenerator.h"
#include "ResourceTexture.h"
#include "ResourceMesh.h"

#include "ComponentParticleEmitter.h"
#include "ComponentTransform.h"
#include "ComponentText.h"
#include "ComponentCamera.h"
#include "ComponentMesh.h"

#include "ModuleTimeManager.h"
#include "ModulePhysics.h"
#include "ModuleParticles.h"
#include "ModuleTextures.h"
#include "ModuleResourceManager.h"
#include "ModuleFileSystem.h"
#include "ModuleSceneManager.h"
#include "ModuleSelection.h"
#include "ResourceScene.h"
#include "ModuleGui.h"

#include "Particle.h"
#include "CurveEditor.h"

#include "PhysX_3.4/Include/extensions/PxDefaultAllocator.h"
#include "PhysX_3.4/Include/extensions/PxDefaultErrorCallback.h"

#include "PhysX_3.4/Include/PxPhysicsAPI.h"

#include "mmgr/mmgr.h"


using namespace Broken;

ComponentParticleEmitter::ComponentParticleEmitter(GameObject* ContainerGO) :Component(ContainerGO, Component::ComponentType::ParticleEmitter)
{
	name = "Particle Emitter";
	Enable();

	App->particles->AddEmitter(this);
	particles.resize(maxParticles);

	for (int i = 0; i < maxParticles; ++i)
	{
		particles[i] = new Particle();
		particles[i]->emitter = this;
	}

	particles_mesh = App->scene_manager->plane;
	texture = (ResourceTexture*)App->resources->CreateResource(Resource::ResourceType::TEXTURE, "DefaultTexture");
	App->renderer3D->particleEmitters.push_back(this);

	float4 whiteColor(1, 1, 1, 1);
	colors.push_back(whiteColor);

	if (scaleCurve == nullptr) {
		scaleCurve = new CurveEditor("##scalex", LINEAR);
		scaleCurve->Init();
		curves.push_back(scaleCurve);
	}
	if (rotateCurve == nullptr) {
		rotateCurve = new CurveEditor("##rotation", LINEAR);
		rotateCurve->Init();
		curves.push_back(rotateCurve);
	}
	if (scaleCurveY == nullptr) {
		scaleCurveY = new CurveEditor("##scaley", LINEAR);
		scaleCurveY->Init();
		curves.push_back(scaleCurveY);
	}
	if (scaleCurveZ == nullptr) {
		scaleCurveZ = new CurveEditor("##scalez", LINEAR);
		scaleCurveZ->Init();
		curves.push_back(scaleCurveZ);
	}
}

ComponentParticleEmitter::~ComponentParticleEmitter()
{
	drawingIndices.clear();

	App->particles->DeleteEmitter(this);

	for (int i = 0; i < maxParticles; ++i) {

		App->particles->particlesToDraw.erase(particles[i]->distanceToCam);
		delete particles[i];
		particles[i] = nullptr;
	}

	if (particleSystem && App->physics->mScene) {
		particleSystem->releaseParticles();
		App->physics->DeleteActor(particleSystem);
		indexPool->release();
		particles.clear();
	}

	if (texture)
	{
		texture->RemoveUser(GO);
		texture->Release();
	}

	if (particles_mesh 	&& particles_mesh->GetUID() != App->scene_manager->plane->GetUID())
	{
		particles_mesh->RemoveUser(GO);
		particles_mesh->Release();
	}

	for (std::vector<ComponentParticleEmitter*>::iterator it = App->renderer3D->particleEmitters.begin(); it != App->renderer3D->particleEmitters.end(); it++) {
		if ((*it) == this) {
			App->renderer3D->particleEmitters.erase(it);
			break;
		}
	}

	for (int i = 0; i < particleMeshes.size(); ++i) {
		particleMeshes[i]->FreeMemory();
		delete particleMeshes[i];
		particleMeshes[i] = nullptr;
	}
	particleMeshes.clear();

	for (int i = 0; i < curves.size(); ++i) {
		delete curves[i];
		curves[i] = nullptr;
	}
	curves.clear();
}

void ComponentParticleEmitter::Update()
{
	if (App->selection->IsSelected(GO))
		DrawEmitterArea();

	if (!animation || !createdAnim) {
		if (particleMeshes.size() > 0) {
			for (int i = 0; i < particleMeshes.size(); ++i) {
				particleMeshes[i]->FreeMemory();
				delete particleMeshes[i];
				particleMeshes[i] = nullptr;
			}
			particleMeshes.clear();
		}
		createdAnim = false;
	}

	if (animation && !createdAnim){
		CreateAnimation(tileSize_X, tileSize_Y);
		createdAnim = true;
	}

	if (to_delete)
		this->GetContainerGameObject()->RemoveComponent(this);
}

void ComponentParticleEmitter::Enable()
{
	particleSystem = App->physics->mPhysics->createParticleSystem(maxParticles, perParticleRestOffset);
	particleSystem->setMaxMotionDistance(100);

	if (particleSystem)
		App->physics->AddParticleActor(particleSystem, GO);

	indexPool = physx::PxParticleExt::createIndexPool(maxParticles);

	particleSystem->setExternalAcceleration(externalAcceleration);
	active = true;
	firstEmision = true;
}

void ComponentParticleEmitter::Disable()
{

	App->physics->DeleteActor(particleSystem);

	active = false;
}

void ComponentParticleEmitter::UpdateParticles(float dt)
{
	int currentPlayTime = App->time->GetGameplayTimePassed() * 1000;

	//Create particle depending on the time
	if (emisionActive && App->GetAppState() == AppState::PLAY && !App->time->gamePaused) {
		if ((currentPlayTime - spawnClock > emisionRate )||playNow)
		{
			uint newParticlesAmount = ((currentPlayTime - spawnClock) / emisionRate) * particlesPerCreation;

			if (!firstEmision)
				CreateParticles(newParticlesAmount);
			else
			{
				if (newParticlesAmount > particlesPerCreation || playNow)
					CreateParticles(particlesPerCreation);
				else
					CreateParticles(newParticlesAmount);

				firstEmision = false;
			}
			playNow = false;
		}

		if (emisionActive && !loop)
		{
			if ((currentPlayTime)-emisionStart > duration)
				emisionActive = false;
		}
	}

	//Update particles
	//lock SDK buffers of *PxParticleSystem* ps for reading
	physx::PxParticleReadData* rd = particleSystem->lockParticleReadData();

	std::vector<physx::PxU32> indicesToErease;
	uint particlesToRelease = 0;

	float3 globalPosition = GO->GetComponent<ComponentTransform>()->GetGlobalPosition();

	bool aabbInCamera = App->renderer3D->culling_camera->frustum.Intersects(particlesAreaAABB);

	// access particle data from physx::PxParticleReadData
	if (rd)
	{
		physx::PxStrideIterator<const physx::PxParticleFlags> flagsIt(rd->flagsBuffer);
		physx::PxStrideIterator<const physx::PxVec3> positionIt(rd->positionBuffer);

		for (unsigned i = 0; i < rd->validParticleRange; ++i, ++flagsIt, ++positionIt)
		{
			bool toDelete = false;
			if (*flagsIt & physx::PxParticleFlag::eVALID)
			{
				//-- CHECK DELETE --
				if (currentPlayTime - particles[i]->spawnTime > particles[i]->lifeTime) {
					indicesToErease.push_back(i);
					particlesToRelease++;
					toDelete = true;
					continue;
				}

				float diff_time = (App->time->GetGameplayTimePassed() * 1000 - particles[i]->spawnTime);

				// -- SCALE --
				if (scaleconstants == 2) {
					scaleOverTime = scaleCurve->GetCurrentValue(diff_time, particles[i]->lifeTime);
					if (separateAxisScale) {
						particles[i]->scale.x = scaleCurve->GetCurrentValue(diff_time, particles[i]->lifeTime);
						particles[i]->scale.y = scaleCurveY->GetCurrentValue(diff_time, particles[i]->lifeTime);
						particles[i]->scale.z = scaleCurveZ->GetCurrentValue(diff_time, particles[i]->lifeTime);
					}
					else {
						particles[i]->scale.x = scaleOverTime;
						particles[i]->scale.y = scaleOverTime;
						particles[i]->scale.z = scaleOverTime;
					}
				}

				if (aabbInCamera) {
					particles[i]->position = float3(positionIt->x, positionIt->y, positionIt->z);

					// -- Follow emitter rotation --
					if (followEmitterRotation) {

						Quat totalRotation = Quat::identity;
						Quat externalRotation = Quat::identity;

						Quat globalRotation;
						float3 scale_, position_;

						switch (rotationType)
						{
						case Broken::ROTATION_PARENT::GO_LOCAL:
							totalRotation = GO->GetComponent<ComponentTransform>()->rotation * emitterRotation;
							externalRotation = GO->GetComponent<ComponentTransform>()->rotation;
							break;
						case Broken::ROTATION_PARENT::GO_GLOBAL:
							GO->GetComponent<ComponentTransform>()->GetGlobalTransform().Decompose(position_, globalRotation, scale_);
							totalRotation = globalRotation * emitterRotation;
							externalRotation = globalRotation;
							break;
						case Broken::ROTATION_PARENT::NONE:
							totalRotation = emitterRotation;
							break;
						}

						float3 newPosition = particles[i]->position;
						Quat newPositionQuat = Quat(newPosition.x - particles[i]->emitterSpawnPosition.x, newPosition.y - particles[i]->emitterSpawnPosition.y, newPosition.z - particles[i]->emitterSpawnPosition.z, 0);
						Quat rotationIncrease = particles[i]->intialRotation.Inverted() /**externalRotation*/;
						newPositionQuat = rotationIncrease * newPositionQuat * rotationIncrease.Conjugated();
						newPositionQuat = externalRotation * newPositionQuat * externalRotation.Conjugated();

						particles[i]->position = globalPosition;
						particles[i]->position += float3(newPositionQuat.x, newPositionQuat.y, newPositionQuat.z);

					}
					else if (followEmitterPosition) {
						particles[i]->position += globalPosition - particles[i]->emitterSpawnPosition;
					}
				}
				if (colorGradient && gradients.size() > 0)
				{
					if (particles[i]->currentGradient >= gradients.size())//Comment this and next line in case gradient widget is applyed
						particles[i]->currentGradient = gradients.size() - 1;
					particles[i]->color += gradients[particles[i]->currentGradient] * dt * 1000;

					if ((currentPlayTime - particles[i]->gradientTimer > colorDuration) && (particles[i]->currentGradient < gradients.size() - 1))
					{
						particles[i]->currentGradient++;
						particles[i]->gradientTimer = currentPlayTime;
					}

				}

				if (particles[i]->scale.x < 0)
					particles[i]->scale.x = 0;

				if (particles[i]->scale.y < 0)
					particles[i]->scale.y = 0;

				if (particles[i]->scale.z < 0)
					particles[i]->scale.z = 0;

				//Choose Frame Animation
				if (animation && particleMeshes.size() > 0)
				{
					int time = currentPlayTime - particles[i]->spawnTime;
					int index = (particleMeshes.size() * time) / (particles[i]->lifeTime / cycles);
					particles[i]->particle_mesh = particleMeshes[(index + particles[i]->startFrame) % particleMeshes.size()];
				}
				else
					particles[i]->particle_mesh = particles_mesh;

				if (rotationconstants == 2)
					particles[i]->rotationSpeed.z = rotateCurve->GetCurrentValue(diff_time, particles[i]->lifeTime);

				if (rotationOvertime1 != 0)
					particles[i]->rotation += particles[i]->rotationSpeed * DEGTORAD * dt;

			}
		}
		// return ownership of the buffers back to the SDK
		rd->unlock();
	}

	if (particlesToRelease > 0) {

		particleSystem->releaseParticles(particlesToRelease, physx::PxStrideIterator<physx::PxU32>(indicesToErease.data()));
		validParticles -= particlesToRelease;
		indexPool->freeIndices(particlesToRelease, physx::PxStrideIterator<physx::PxU32>(indicesToErease.data()));
	}

	UpdateAABBs();
	if (aabbInCamera)
		SortParticles();
}

void ComponentParticleEmitter::SortParticles()
{
	physx::PxParticleReadData* rd = particleSystem->lockParticleReadData();
	if (rd)
	{
		physx::PxStrideIterator<const physx::PxParticleFlags> flagsIt(rd->flagsBuffer);

		for (unsigned i = 0; i < rd->validParticleRange; ++i, ++flagsIt)
		{
			if (*flagsIt & physx::PxParticleFlag::eVALID)
			{

				float distance = 1.0f/App->renderer3D->active_camera->frustum.NearPlane().Distance(particles[i]->position);

				//drawingIndices[1.0f / distance] = i;

				bool particleSent = false;
				while (!particleSent) {
					if (App->particles->particlesToDraw.find(distance) == App->particles->particlesToDraw.end()) {
						App->particles->particlesToDraw[distance] = particles[i];
						particles[i]->distanceToCam = distance;
						particleSent = true;
					}
					else
						distance -= 0.00001;

				}
			}
		}

		// return ownership of the buffers back to the SDK
		rd->unlock();
	}
}

void ComponentParticleEmitter::SetEmitterBlending() const
{
	bool blendEq_Same = (m_PartBlEquation == App->renderer3D->GetRendererBlendingEquation());
	if (m_PartAutoBlending)
	{
		//if (m_PartBlendFunc == App->renderer3D->GetRendererBlendAutoFunction() && blendEq_Same)
		//	return;

		App->renderer3D->PickBlendingAutoFunction(m_PartBlendFunc, m_PartBlEquation);
		App->renderer3D->m_ChangedBlending = true;
	}
	else
	{
		BlendingTypes src, dst;
		App->renderer3D->GetRendererBlendingManualFunction(src, dst);

		bool manualBlend_Same = (m_MPartBlend_Src == src && m_MPartBlend_Dst == dst);
		if (manualBlend_Same && blendEq_Same)
			return;

		App->renderer3D->PickBlendingManualFunction(m_MPartBlend_Src, m_MPartBlend_Dst, m_PartBlEquation);
		App->renderer3D->m_ChangedBlending = true;
	}
}

void ComponentParticleEmitter::DrawParticles(bool shadowsPass)
{
	if (!active || drawingIndices.empty())
		return;

	// --- Blending ---
	SetEmitterBlending();

	if (!shadowsPass)
	{
		if(particlesFaceCulling)
			glEnable(GL_CULL_FACE);
		else
			glDisable(GL_CULL_FACE);
	}

	// -- Frustum culling --
	Plane cameraPlanes[6];
	App->renderer3D->culling_camera->frustum.GetPlanes(cameraPlanes);
	std::map<float, int>::iterator it = drawingIndices.begin();

	while (it != drawingIndices.end())
	{
		int paco = (*it).second;

		//Check if the particles are inside the frustum of the camera
		bool draw = true;
		for (int i = 0; i < 6; ++i)
		{
			//If the particles is on the positive side of one ore more planes, it's outside the frustum
			if (cameraPlanes[i].IsOnPositiveSide(particles[paco]->position) || (shadowsPass && !m_CastShadows) || (!shadowsPass && m_OnlyShadows))
			{
				draw = false;
				break;
			}
		}

		if (draw)
			particles[paco]->Draw(shadowsPass);

		it++;
	}

	if (!shadowsPass)
	{
		if (!particlesFaceCulling)
			glEnable(GL_CULL_FACE);

		drawingIndices.clear();
	}
}

void ComponentParticleEmitter::ChangeParticlesColor(float4 color)
{
	color /= 255.0f;

	colors[0] = color;
	UpdateAllGradients();

	for (int i = 0; i < maxParticles; ++i)
		particles[i]->color = color;

}

json ComponentParticleEmitter::Save() const
{
	json node;

	node["PlayOnAwake"] = playOnAwake;

	node["Active"] = this->active;

	node["positionX"] = std::to_string(emitterPosition.x);
	node["positionY"] = std::to_string(emitterPosition.y);
	node["positionZ"] = std::to_string(emitterPosition.z);

	node["rotationX"] = std::to_string(eulerRotation.x);
	node["rotationY"] = std::to_string(eulerRotation.y);
	node["rotationZ"] = std::to_string(eulerRotation.z);

	node["sizeX"] = std::to_string(size.x);
	node["sizeY"] = std::to_string(size.y);
	node["sizeZ"] = std::to_string(size.z);

	node["emisionRate"] = std::to_string(emisionRate);

	node["particlesPerCreation"] = std::to_string(particlesPerCreation);

	node["externalAccelerationX"] = std::to_string(externalAcceleration.x);
	node["externalAccelerationY"] = std::to_string(externalAcceleration.y);
	node["externalAccelerationZ"] = std::to_string(externalAcceleration.z);

	node["particlesVelocityX"] = std::to_string(particlesVelocity.x);
	node["particlesVelocityY"] = std::to_string(particlesVelocity.y);
	node["particlesVelocityZ"] = std::to_string(particlesVelocity.z);

	node["velocityRandomFactor1X"] = std::to_string(velocityRandomFactor1.x);
	node["velocityRandomFactor1Y"] = std::to_string(velocityRandomFactor1.y);
	node["velocityRandomFactor1Z"] = std::to_string(velocityRandomFactor1.z);
	node["velocityRandomFactor2X"] = std::to_string(velocityRandomFactor2.x);
	node["velocityRandomFactor2Y"] = std::to_string(velocityRandomFactor2.y);
	node["velocityRandomFactor2Z"] = std::to_string(velocityRandomFactor2.z);
	node["velocityconstants"] = std::to_string(velocityconstants);

	node["rotationType"] = std::to_string(rotationTypeInt);

	node["followEmitterPosition"] = followEmitterPosition;
	node["followEmitterRotation"] = followEmitterRotation;

	node["particlesLifeTime"] = std::to_string(particlesLifeTime);

	node["animation"] = std::to_string(animation);
	node["tiles_X"] = std::to_string(tileSize_X);
	node["tiles_Y"] = std::to_string(tileSize_Y);
	node["cycles"] = std::to_string(cycles);
	node["startFrame"] = std::to_string(startFrame);
	node["randomStartFrame"] = randomStartFrame;

	node["num_colors"] = std::to_string(colors.size());

	for (int i = 0; i < colors.size(); ++i) {
		node["colors"][i]["x"] = std::to_string(colors[i].x);
		node["colors"][i]["y"] = std::to_string(colors[i].y);
		node["colors"][i]["z"] = std::to_string(colors[i].z);
		node["colors"][i]["a"] = std::to_string(colors[i].w);
	}

	node["num_gradients"] = std::to_string(gradients.size());
	for (int i = 0; i < gradients.size(); ++i) {
		node["gradients"][i]["x"] = std::to_string(gradients[i].x);
		node["gradients"][i]["y"] = std::to_string(gradients[i].y);
		node["gradients"][i]["z"] = std::to_string(gradients[i].z);
		node["gradients"][i]["a"] = std::to_string(gradients[i].w);
	}

	node["grad_duration"] = std::to_string(colorDuration);

	node["GradientColor"] = colorGradient;

	node["Loop"] = loop;
	node["Duration"] = std::to_string(duration);
	node["HorizontalBill"] = std::to_string((int)horizontalBillboarding);
	node["VerticalBill"] = std::to_string((int)verticalBillboarding);
	node["ParticlesBill"] = particlesBillboarding;
	node["CollisionsActivated"] = collision_active;
	node["ParticlesFaceCulling"] = particlesFaceCulling;

	node["particlesScaleX"] = std::to_string(particlesScale.x);
	node["particlesScaleY"] = std::to_string(particlesScale.y);
	node["particlesScaleZ"] = std::to_string(particlesScale.z);

	node["particleScaleRandomFactor1"] = std::to_string(particlesScaleRandomFactor1);
	node["particleScaleRandomFactor2"] = std::to_string(particlesScaleRandomFactor2);
	node["scaleconstants"] = std::to_string(scaleconstants);

	node["particleScaleOverTime"] = std::to_string(scaleOverTime);

	node["ParticlesCustomMesh"] = custom_mesh;
	node["Resources"]["ResourceTexture"];
	node["Resources"]["ResourceMesh"];

	if (texture)
		node["Resources"]["ResourceTexture"] = std::string(texture->GetResourceFile());

	if (custom_mesh)
		if (particles_mesh->GetUID() != App->scene_manager->plane->GetUID())
			node["Resources"]["ResourceMesh"] = std::string(particles_mesh->GetResourceFile());


	node["separateAxis"] = std::to_string(separateAxis);
	node["rotationOvertime1"][0] = std::to_string(rotationOvertime1[0]);
	node["rotationOvertime1"][1] = std::to_string(rotationOvertime1[1]);
	node["rotationOvertime1"][2] = std::to_string(rotationOvertime1[2]);
	node["rotationOvertime2"][0] = std::to_string(rotationOvertime2[0]);
	node["rotationOvertime2"][1] = std::to_string(rotationOvertime2[1]);
	node["rotationOvertime2"][2] = std::to_string(rotationOvertime2[2]);
	node["rotationconstants"] = std::to_string(rotationconstants);

	node["randomInitialRotation"] = randomInitialRotation;

	node["minInitialRotation"][0] = std::to_string(minInitialRotation[0]);
	node["minInitialRotation"][1]= std::to_string(minInitialRotation[1]);
	node["minInitialRotation"][2]= std::to_string(minInitialRotation[2]);
	node["maxInitialRotation"][0]= std::to_string(maxInitialRotation[0]);
	node["maxInitialRotation"][1]= std::to_string(maxInitialRotation[1]);
	node["maxInitialRotation"][2]= std::to_string(maxInitialRotation[2]);

	node["separateAxisScale"] = std::to_string(separateAxisScale);
	node["num_curves"] = std::to_string(curves.size());
	for (int i = 0; i < curves.size(); ++i) {
		CurveEditor* curve = curves[i];
		node["curves"][i]["num_points"] = std::to_string(curve->pointsCurveTangents.size());
		node["curves"][i]["name"] = curve->name.c_str();
		node["curves"][i]["type"] = std::to_string(curve->type);
		node["curves"][i]["multiplier"] = std::to_string(curve->multiplier);
		for (int j = 0; j < curve->pointsCurveTangents.size(); ++j) {
			node["curves"][i][std::to_string(j).c_str()]["PrevX"] = std::to_string(curve->pointsCurveTangents[j].prev_tangent.x);
			node["curves"][i][std::to_string(j).c_str()]["PrevY"] = std::to_string(curve->pointsCurveTangents[j].prev_tangent.y);
			node["curves"][i][std::to_string(j).c_str()]["PX"] = std::to_string(curve->pointsCurveTangents[j].p.x);
			node["curves"][i][std::to_string(j).c_str()]["PY"] = std::to_string(curve->pointsCurveTangents[j].p.y);
			node["curves"][i][std::to_string(j).c_str()]["NextX"] = std::to_string(curve->pointsCurveTangents[j].next_tangent.x);
			node["curves"][i][std::to_string(j).c_str()]["NextY"] = std::to_string(curve->pointsCurveTangents[j].next_tangent.y);
		}
	}

	// --- Blend Save ---
	node["PartBlendEquation"] = (int)m_PartBlEquation;
	node["PartBlendFunc"] = (int)m_PartBlendFunc;
	node["PartMBlFuncSrc"] = (int)m_MPartBlend_Src;
	node["PartMBlFuncDst"] = (int)m_MPartBlend_Dst;
	node["PartAutoBlending"] = m_PartAutoBlending;

	// --- Lighting Save ---
	node["PartLightAffected"] = m_AffectedByLight;
	node["PartSceneColorAffected"] = m_AffectedBySceneColor;
	node["PartCastShadows"] = m_CastShadows;
	node["PartReceiveShadows"] = m_ReceiveShadows;
	node["PartOnlyShadows"] = m_OnlyShadows;

	return node;
}

void ComponentParticleEmitter::Load(json& node)
{
	for (int i = 0; i < curves.size(); ++i) {
		delete curves[i];
	}
	curves.clear();
	scaleCurve = nullptr;
	scaleCurveY = nullptr;
	scaleCurveZ = nullptr;
	rotateCurve = nullptr;

	this->active = node.contains("Active") ? (bool)node["Active"] : false;

	//load the strings
	std::string LpositionX = node.contains("positionX") ? node["positionX"] : "0";
	std::string LpositionY = node.contains("positionY") ? node["positionY"] : "0";
	std::string LpositionZ = node.contains("positionZ") ? node["positionZ"] : "0";

	std::string LrotationX = node.contains("rotationX") ? node["rotationX"] : "0";
	std::string LrotationY = node.contains("rotationY") ? node["rotationY"] : "0";
	std::string LrotationZ = node.contains("rotationZ") ? node["rotationZ"] : "0";

	std::string Lsizex = node.contains("sizeX") ? node["sizeX"] : "0";
	std::string Lsizey = node.contains("sizeY") ? node["sizeY"] : "0";
	std::string Lsizez = node.contains("sizeZ") ? node["sizeZ"] : "0";

	std::string LemisionRate = node.contains("emisionRate") ? node["emisionRate"] : "0"; // typo: emission

	std::string LparticlesPerCreation = node.contains("particlesPerCreation") ? node["particlesPerCreation"] : "1";

	std::string LexternalAccelerationx = node.contains("externalAccelerationX") ? node["externalAccelerationX"] : "0";
	std::string LexternalAccelerationy = node.contains("externalAccelerationY") ? node["externalAccelerationY"] : "0";
	std::string LexternalAccelerationz = node.contains("externalAccelerationZ") ? node["externalAccelerationZ"] : "0";

	std::string LparticlesVelocityx = node.contains("particlesVelocityX") ? node["particlesVelocityX"] : "0";
	std::string LparticlesVelocityy = node.contains("particlesVelocityY") ? node["particlesVelocityY"] : "0";
	std::string LparticlesVelocityz = node.contains("particlesVelocityZ") ? node["particlesVelocityZ"] : "0";

	std::string LvelocityRandomFactor1x = node.contains("velocityRandomFactor1X") ? node["velocityRandomFactor1X"] : "0";
	std::string LvelocityRandomFactor1y = node.contains("velocityRandomFactor1Y") ? node["velocityRandomFactor1Y"] : "0";
	std::string LvelocityRandomFactor1z = node.contains("velocityRandomFactor1Z") ? node["velocityRandomFactor1Z"] : "0";
	std::string LvelocityRandomFactor2x = node.contains("velocityRandomFactor2X") ? node["velocityRandomFactor2X"] : "0";
	std::string LvelocityRandomFactor2y = node.contains("velocityRandomFactor2Y") ? node["velocityRandomFactor2Y"] : "0";
	std::string LvelocityRandomFactor2z = node.contains("velocityRandomFactor2Z") ? node["velocityRandomFactor2Z"] : "0";
	std::string _velocityconstants = node.contains("velocityconstants") ? node["velocityconstants"] : "0";

	std::string LparticlesLifeTime = node.contains("particlesLifeTime") ? node["particlesLifeTime"] : "0";
	std::string LparticlesLifeTime1 = node.contains("particlesLifeTime1") ? node["particlesLifeTime1"] : "0";
	std::string LparticlesLifeTime2 = node.contains("particlesLifeTime2") ? node["particlesLifeTime2"] : "0";
	std::string _lifetimeconstants = node.contains("lifetimeconstants") ? node["lifetimeconstants"] : "0";

	followEmitterPosition = node.contains("followEmitterPosition") ? node["followEmitterPosition"].get<bool>() : false;
	followEmitterRotation = node.contains("followEmitterRotation") ? node["followEmitterRotation"].get<bool>() : false;

	playOnAwake = node.contains("PlayOnAwake") ? node["PlayOnAwake"].get<bool>() : false;
	emisionActive = playOnAwake;

	std::string LParticlesSize = node.contains("particlesSize") ? node["particlesSize"] : "0";

	std::string _animation = node.contains("animation") ? node["animation"] : "0";
	std::string _tiles_X = node.contains("tiles_X") ? node["tiles_X"] : "1";
	std::string _tiles_Y = node.contains("tiles_Y") ? node["tiles_Y"] : "1";
	std::string _cycles = node.contains("cycles") ? node["cycles"] : "1";
	std::string _startFrame = node.contains("startFrame") ? node["startFrame"] : "0";

	randomStartFrame = node.contains("randomStartFrame") ? node["randomStartFrame"].get<bool>() : true;

	std::string LDuration = node.contains("Duration") ? node["Duration"] : "0";

	std::string LParticlesScaleX = node.contains("particlesScaleX") ? node["particlesScaleX"] : "1";
	std::string LParticlesScaleY = node.contains("particlesScaleY") ? node["particlesScaleY"] : "1";
	std::string LParticlesScaleZ = node.contains("particlesScaleZ") ? node["particlesScaleZ"] : "1";

	std::string LParticleScaleRandomFactor1 = node.contains("particleScaleRandomFactor1") ? node["particleScaleRandomFactor1"] : "1";
	std::string LParticleScaleRandomFactor2 = node.contains("particleScaleRandomFactor2") ? node["particleScaleRandomFactor2"] : "1";

	std::string LScaleOverTime = node.contains("particleScaleOverTime") ? node["particleScaleOverTime"] : "0";

	std::string _num_colors = node.contains("num_colors") ? node["num_colors"] : "0";
	std::string _num_gradients = node.contains("num_gradients") ? node["num_gradients"] : "0";
	std::string _gradientDuration = node.contains("grad_duration") ? node["grad_duration"] : "0";
	std::string _num_curves = node.contains("num_curves") ? node["num_curves"] : "0";
	std::string _separateAxisScale = node.contains("separateAxisScale") ? node["separateAxisScale"] : "0";

	std::string _separateAxis = node.contains("separateAxis") ? node["separateAxis"] : "0";
	std::string rotationOvertime1_X = node["rotationOvertime1"][0].is_null() ? "0" : node["rotationOvertime1"][0];
	std::string rotationOvertime1_Y = node["rotationOvertime1"][1].is_null() ? "0" : node["rotationOvertime1"][1];
	std::string rotationOvertime1_Z = node["rotationOvertime1"][2].is_null() ? "0" : node["rotationOvertime1"][2];
	std::string rotationOvertime2_X = node["rotationOvertime2"][0].is_null() ? "0" : node["rotationOvertime2"][0];
	std::string rotationOvertime2_Y = node["rotationOvertime2"][1].is_null() ? "0" : node["rotationOvertime2"][1];
	std::string rotationOvertime2_Z = node["rotationOvertime2"][2].is_null() ? "0" : node["rotationOvertime2"][2];
	std::string _rotationconstants = node.contains("rotationconstants") ? node["rotationconstants"] : "0";
	std::string _scaleconstants = node.contains("scaleconstants") ? node["scaleconstants"] : "0";

	std::string minInitialRotation_X = node["minInitialRotation"][0].is_null() ? "0" : node["minInitialRotation"][0];
	std::string minInitialRotation_Y = node["minInitialRotation"][1].is_null() ? "0" : node["minInitialRotation"][1];
	std::string minInitialRotation_Z = node["minInitialRotation"][2].is_null() ? "0" : node["minInitialRotation"][2];
	std::string maxInitialRotation_X = node["maxInitialRotation"][0].is_null() ? "0" : node["maxInitialRotation"][0];
	std::string maxInitialRotation_Y = node["maxInitialRotation"][1].is_null() ? "0" : node["maxInitialRotation"][1];
	std::string maxInitialRotation_Z = node["maxInitialRotation"][2].is_null() ? "0" : node["maxInitialRotation"][2];

	randomInitialRotation = node.contains("randomInitialRotation") ? node["randomInitialRotation"].get<bool>() : false;


	std::string rotation_type = node.contains("rotationType") ? node["rotationType"] : "0";
	rotationTypeInt = std::stoi(rotation_type);
	rotationType = ROTATION_PARENT(rotationTypeInt);

	colorDuration = std::atoi(_gradientDuration.c_str());
	int num = std::stof(_num_colors);
	if (num != 0) {
		colors.pop_back();
		for (int i = 0; i < num; ++i) {
			std::string color_x = node["colors"][i].contains("x") ? node["colors"][i]["x"] : "255";
			std::string color_y = node["colors"][i].contains("y") ? node["colors"][i]["y"] : "255";
			std::string color_z = node["colors"][i].contains("z") ? node["colors"][i]["z"] : "255";
			std::string color_a = node["colors"][i].contains("a") ? node["colors"][i]["a"] : "255";
			float4 color = float4(std::stof(color_x), std::stof(color_y), std::stof(color_z), std::stof(color_a));
			colors.push_back(color);
		}
	}

	if (!node["GradientColor"].is_null())
		colorGradient = node["GradientColor"];
	else
		colorGradient = false;

	num = std::stof(_num_gradients);
	for (int i = 0; i < num; ++i) {
		std::string color_x = node["gradients"][i].contains("x") ? node["gradients"][i]["x"] : "255";
		std::string color_y = node["gradients"][i].contains("y") ? node["gradients"][i]["y"] : "255";
		std::string color_z = node["gradients"][i].contains("z") ? node["gradients"][i]["z"] : "255";
		std::string color_a = node["gradients"][i].contains("a") ? node["gradients"][i]["a"] : "255";
		float4 color = float4(std::stof(color_x), std::stof(color_y), std::stof(color_z), std::stof(color_a));
		gradients.push_back(color);
	}

	num = std::stof(_num_curves);
	for (int i = 0; i < num; ++i) {
		std::string _num_points = node["curves"][i].contains("num_points") ? node["curves"][i]["num_points"] : "0";
		std::string name = node["curves"][i].contains("name") ? node["curves"][i]["name"] : "";
		std::string _type = node["curves"][i].contains("type") ? node["curves"][i]["type"] : "0";
		std::string _multi = node["curves"][i].contains("multiplier") ? node["curves"][i]["multiplier"] : "1";
		int points = std::stof(_num_points);
		CurveEditor* curve = new CurveEditor(name.c_str(), (CurveType)(int)std::stof(_type), std::stof(_multi));
		for (int j = 0; j < points; ++j) {
			std::string prev_tangentX = node["curves"][i][std::to_string(j).c_str()].contains("PrevX") ? node["curves"][i][std::to_string(j).c_str()]["PrevX"] : "0";
			std::string prev_tangentY = node["curves"][i][std::to_string(j).c_str()].contains("PrevY") ? node["curves"][i][std::to_string(j).c_str()]["PrevY"] : "0";
			std::string pX = node["curves"][i][std::to_string(j).c_str()].contains("PX") ? node["curves"][i][std::to_string(j).c_str()]["PX"] : "0";
			std::string pY = node["curves"][i][std::to_string(j).c_str()].contains("PY") ? node["curves"][i][std::to_string(j).c_str()]["PY"] : "0";
			std::string post_tangentX = node["curves"][i][std::to_string(j).c_str()].contains("NextX") ? node["curves"][i][std::to_string(j).c_str()]["NextX"] : "0";
			std::string post_tangentY = node["curves"][i][std::to_string(j).c_str()].contains("NextY") ? node["curves"][i][std::to_string(j).c_str()]["NextY"] : "0";
			Point p;
			p.prev_tangent = float2(std::stof(prev_tangentX), std::stof(prev_tangentY));
			p.p = float2(std::stof(pX), std::stof(pY));
			p.next_tangent = float2(std::stof(post_tangentX), std::stof(post_tangentY));
			curve->pointsCurveTangents.push_back(p);
		}
		curves.push_back(curve);
	}
	switch (num)
	{
	default:
	case 4:
		scaleCurveZ = curves[3];
	case 3:
		scaleCurveY = curves[2];
	case 2:
		rotateCurve = curves[1];
	case 1:
		scaleCurve = curves[0];
	case 0:
		break;
	}

	if (!node["Loop"].is_null())
		loop = node["Loop"];
	else
		loop = true;


	// Load Custom Mesh
	custom_mesh = node.find("ParticlesCustomMesh") == node.end() ? false : node["ParticlesCustomMesh"].get<bool>();
	if (custom_mesh)
	{
		std::string MeshPath = node["Resources"]["ResourceMesh"].is_null() ? "0" : node["Resources"]["ResourceMesh"];
		App->fs->SplitFilePath(MeshPath.c_str(), nullptr, &MeshPath);
		MeshPath = MeshPath.substr(0, MeshPath.find_last_of("."));

		ResourceMesh* auxMesh = (ResourceMesh*)App->resources->GetResource(std::stoi(MeshPath));
		if (auxMesh)
			particles_mesh = auxMesh;
		else
			particles_mesh = App->scene_manager->plane;
	}
	else
		particles_mesh = App->scene_manager->plane;

	if(particles_mesh)
		particles_mesh->AddUser(GO);

	// Load Texture
	std::string path = node["Resources"]["ResourceTexture"].is_null() ? "0" : node["Resources"]["ResourceTexture"];
	App->fs->SplitFilePath(path.c_str(), nullptr, &path);
	path = path.substr(0, path.find_last_of("."));

	ResourceTexture* auxText = (ResourceTexture*)App->resources->GetResource(std::stoi(path));

	if (auxText != nullptr)
		texture = auxText;

	if (texture)
		texture->AddUser(GO);

	//Pass the strings to the needed dada types
	emitterPosition.x = std::stof(LpositionX);
	emitterPosition.y = std::stof(LpositionY);
	emitterPosition.z = std::stof(LpositionZ);

	eulerRotation.x = std::stof(LrotationX);
	eulerRotation.y = std::stof(LrotationY);
	eulerRotation.z = std::stof(LrotationZ);

	emitterRotation = Quat::FromEulerXYZ(eulerRotation.x * DEGTORAD, eulerRotation.y * DEGTORAD, eulerRotation.z * DEGTORAD);

	size.x = std::stof(Lsizex);
	size.y = std::stof(Lsizey);
	size.z = std::stof(Lsizez);

	emisionRate = std::stof(LemisionRate);

	particlesPerCreation = std::stoi(LparticlesPerCreation);

	externalAcceleration.x = std::stof(LexternalAccelerationx);
	externalAcceleration.y = std::stof(LexternalAccelerationy);
	externalAcceleration.z = std::stof(LexternalAccelerationz);
	particleSystem->setExternalAcceleration(externalAcceleration);

	particlesVelocity.x = std::stof(LparticlesVelocityx);
	particlesVelocity.y = std::stof(LparticlesVelocityy);
	particlesVelocity.z = std::stof(LparticlesVelocityz);

	velocityRandomFactor1.x = std::stof(LvelocityRandomFactor1x);
	velocityRandomFactor1.y = std::stof(LvelocityRandomFactor1y);
	velocityRandomFactor1.z = std::stof(LvelocityRandomFactor1z);
	velocityRandomFactor2.x = std::stof(LvelocityRandomFactor2x);
	velocityRandomFactor2.y = std::stof(LvelocityRandomFactor2y);
	velocityRandomFactor2.z = std::stof(LvelocityRandomFactor2z);
	velocityconstants = std::stof(_velocityconstants);

	particlesLifeTime = std::stof(LparticlesLifeTime);
	particlesLifeTime1 = std::stof(LparticlesLifeTime1);
	particlesLifeTime2 = std::stof(LparticlesLifeTime2);
	lifetimeconstants = std::stof(_lifetimeconstants);


	duration = std::stoi(LDuration);

	particlesScale.x = std::stof(LParticlesScaleX);
	particlesScale.y = std::stof(LParticlesScaleY);
	particlesScale.z = std::stof(LParticlesScaleZ);

	particlesScaleRandomFactor1 = std::stof(LParticleScaleRandomFactor1);
	particlesScaleRandomFactor2 = std::stof(LParticleScaleRandomFactor2);

	scaleOverTime = std::stof(LScaleOverTime);

	animation = std::stof(_animation);
	tileSize_X = std::stof(_tiles_X);
	tileSize_Y = std::stof(_tiles_Y);
	cycles = std::stof(_cycles);
	startFrame = std::stof(_startFrame);

	separateAxis = std::stof(_separateAxis);
	separateAxisScale = std::stof(_separateAxisScale);
	rotationOvertime1[0] = std::stof(rotationOvertime1_X);
	rotationOvertime1[1] = std::stof(rotationOvertime1_Y);
	rotationOvertime1[2] = std::stof(rotationOvertime1_Z);
	rotationOvertime2[0] = std::stof(rotationOvertime2_X);
	rotationOvertime2[1] = std::stof(rotationOvertime2_Y);
	rotationOvertime2[2] = std::stof(rotationOvertime2_Z);
	rotationconstants = std::stof(_rotationconstants);
	scaleconstants = std::stof(_scaleconstants);

	minInitialRotation[0] = std::stoi(minInitialRotation_X);
	minInitialRotation[1] = std::stoi(minInitialRotation_Y);
	minInitialRotation[2] = std::stoi(minInitialRotation_Z);
	maxInitialRotation[0] = std::stoi(maxInitialRotation_X);
	maxInitialRotation[1] = std::stoi(maxInitialRotation_Y);
	maxInitialRotation[2] = std::stoi(maxInitialRotation_Z);


	if (scaleCurve == nullptr) {
		scaleCurve = new CurveEditor("##scalex", LINEAR);
		scaleCurve->Init();
		curves.push_back(scaleCurve);
	}
	if (rotateCurve == nullptr) {
		rotateCurve = new CurveEditor("##rotation", LINEAR);
		rotateCurve->Init();
		curves.push_back(rotateCurve);
	}
	if (scaleCurveY == nullptr && separateAxisScale) {
		scaleCurveY = new CurveEditor("##scaley", LINEAR);
		scaleCurveY->Init();
		curves.push_back(scaleCurveY);
	}
	if (scaleCurveZ == nullptr && separateAxisScale) {
		scaleCurveZ = new CurveEditor("##scalez", LINEAR);
		scaleCurveZ->Init();
		curves.push_back(scaleCurveZ);
	}

	// --- Blending Load ---
	m_PartBlendFunc = node.find("PartBlendFunc") == node.end() ? BlendAutoFunction::STANDARD_INTERPOLATIVE : (BlendAutoFunction)node["PartBlendFunc"].get<int>();
	m_PartBlEquation = node.find("PartBlendEquation") == node.end() ? BlendingEquations::ADD : (BlendingEquations)node["PartBlendEquation"].get<int>();
	m_MPartBlend_Src = node.find("PartMBlFuncSrc") == node.end() ? BlendingTypes::SRC_ALPHA : (BlendingTypes)node["PartMBlFuncSrc"].get<int>();
	m_MPartBlend_Dst = node.find("PartMBlFuncDst") == node.end() ? BlendingTypes::ONE_MINUS_SRC_ALPHA : (BlendingTypes)node["PartMBlFuncDst"].get<int>();
	m_PartAutoBlending = node.find("PartAutoBlending") == node.end() ? true : node["PartAutoBlending"].get<bool>();

	// --- Lighting Save ---
	m_AffectedByLight = node.find("PartLightAffected") == node.end() ? true : node["PartLightAffected"].get<bool>();
	m_AffectedBySceneColor = node.find("PartSceneColorAffected") == node.end() ? true : node["PartSceneColorAffected"].get<bool>();
	m_CastShadows = node.find("PartCastShadows") == node.end() ? true : node["PartCastShadows"].get<bool>();
	m_ReceiveShadows = node.find("PartReceiveShadows") == node.end() ? true : node["PartReceiveShadows"].get<bool>();
	m_OnlyShadows = node.find("PartOnlyShadows") == node.end() ? false : node["PartOnlyShadows"].get<bool>();

	// --- Collisions ---
	collision_active = node.find("CollisionsActivated") == node.end() ? false : node["CollisionsActivated"].get<bool>();
	SetActiveCollisions(collision_active);

	// --- Face Culling ---
	particlesFaceCulling = node.find("ParticlesFaceCulling") == node.end() ? true : node["ParticlesFaceCulling"].get<bool>();

	// --- V/H Billbaording ---
	particlesBillboarding = node.find("ParticlesBill") == node.end() ? true : node["ParticlesBill"].get<bool>();
	horizontalBillboarding = verticalBillboarding = false;
	if (particlesBillboarding)
	{
		if (node.find("HorizontalBill") != node.end())
		{
			std::string hBill = node["HorizontalBill"];
			horizontalBillboarding = (bool)std::stoi(hBill);
		}

		if (node.find("VerticalBill") != node.end())
		{
			std::string vBill = node["VerticalBill"];
			verticalBillboarding = (bool)std::stoi(vBill);
		}
	}
}

void ComponentParticleEmitter::CreateInspectorNode()
{

	//Play on awake
	ImGui::NewLine();
	ImGui::Checkbox("##PlayOnAwake", &playOnAwake);
	if (App->GetAppState() != AppState::PLAY)
		emisionActive = playOnAwake;
	ImGui::SameLine();
	ImGui::Text("Play on awake");

	ImGui::NewLine();

	//Follow emitter position
	ImGui::Checkbox("##SFollow emitter position", &followEmitterPosition);
	ImGui::SameLine(); ImGui::Text("Follow emitter position");

	//Follow emitter position
	ImGui::Checkbox("##SFollow emitter rotation", &followEmitterRotation);
	ImGui::SameLine(); ImGui::Text("Follow emitter rotation & position");
	ImGui::NewLine();


	// --- Loop ---
	if (ImGui::Checkbox("##PELoop", &loop))
		if (loop)
		{
			emisionActive = true;
			firstEmision = true;
		}

	ImGui::SameLine(); ImGui::Text("Loop");

	// Duration
	ImGui::Text("Duration");
	ImGui::SameLine();
	ImGui::DragInt("##PEDuration", &duration);


	ImGui::NewLine();
	ImGui::Text("Rotation type");
	if (ImGui::Combo("##PERotationType", &rotationTypeInt, "GLOBAL ROTATION\0NONE\0\0"))
	{
		rotationType = ROTATION_PARENT(rotationTypeInt);
	}

	//Emitter position
	ImGui::NewLine();
	ImGui::Text("Position");

	ImGui::Text("X");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(ImGui::GetWindowWidth() * 0.15f);

	ImGui::DragFloat("##SPositionX", &emitterPosition.x, 0.05f);

	ImGui::SameLine();

	ImGui::Text("Y");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(ImGui::GetWindowWidth() * 0.15f);

	ImGui::DragFloat("##SPositionY", &emitterPosition.y, 0.05f);

	ImGui::SameLine();

	ImGui::Text("Z");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(ImGui::GetWindowWidth() * 0.15f);

	ImGui::DragFloat("##SPositionZ", &emitterPosition.z, 0.05f);

	//Emitter rotation
	ImGui::Text("Rotation");

	float3 rotation = eulerRotation;
	bool rotationUpdated = false;

	ImGui::Text("X");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(ImGui::GetWindowWidth() * 0.15f);

	if (ImGui::DragFloat("##SRotationX", &rotation.x, 0.15f, -10000.0f, 10000.0f))
		rotationUpdated = true;

	ImGui::SameLine();

	ImGui::Text("Y");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(ImGui::GetWindowWidth() * 0.15f);

	if (ImGui::DragFloat("##SRotationY", &rotation.y, 0.15f, -10000.0f, 10000.0f))
		rotationUpdated = true;

	ImGui::SameLine();

	ImGui::Text("Z");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(ImGui::GetWindowWidth() * 0.15f);

	if (ImGui::DragFloat("##SRotationZ", &rotation.z, 0.15f, -10000.0f, 10000.0f))
		rotationUpdated = true;

	if (rotationUpdated) {
		float3 difference = (rotation - eulerRotation) * DEGTORAD;
		Quat quatrot = Quat::FromEulerXYZ(difference.x, difference.y, difference.z);

		emitterRotation = Quat::FromEulerXYZ(rotation.x * DEGTORAD, rotation.y * DEGTORAD, rotation.z * DEGTORAD);
		eulerRotation = rotation;
	}

	//Emitter size
	ImGui::Text("Emitter size");

	ImGui::Text("X");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(ImGui::GetWindowWidth() * 0.15f);

	ImGui::DragFloat("##SEmitterX", &size.x, 0.05f, 0.10f, 100.0f);

	ImGui::SameLine();

	ImGui::Text("Y");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(ImGui::GetWindowWidth() * 0.15f);

	ImGui::DragFloat("##SEmitterY", &size.y, 0.05f, 0.10f, 100.0f);

	ImGui::SameLine();

	ImGui::Text("Z");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(ImGui::GetWindowWidth() * 0.15f);

	ImGui::DragFloat("##SEmitterZ", &size.z, 0.05f, 0.10f, 100.0f);

	//External forces
	ImGui::Text("External forces ");
	bool forceChanged = false;
	//X
	ImGui::Text("X");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(ImGui::GetWindowWidth() * 0.15f);
	if (ImGui::DragFloat("##SExternalforcesX", &externalAcceleration.x, 0.05f, -50.0f, 50.0f))
		forceChanged = true;

	ImGui::SameLine();
	//Y
	ImGui::Text("Y");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(ImGui::GetWindowWidth() * 0.15f);
	if (ImGui::DragFloat("##SExternalforcesY", &externalAcceleration.y, 0.05f, -50.0f, 50.0f))
		forceChanged = true;
	//Z
	ImGui::SameLine();
	ImGui::Text("Z");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(ImGui::GetWindowWidth() * 0.15f);
	if (ImGui::DragFloat("##SExternalforcesZ", &externalAcceleration.z, 0.05f, -50.0f, 50.0f))
		forceChanged = true;

	if (forceChanged)
		particleSystem->setExternalAcceleration(externalAcceleration);

	//Emision rate
	ImGui::NewLine();
	ImGui::Text("Emision rate (ms)");
	ImGui::SetNextItemWidth(ImGui::GetWindowWidth() * 0.3f);
	ImGui::DragFloat("##SEmision rate", &emisionRate, 1.0f, 1.0f, 100000.0f);

	//Emision rate
	ImGui::Text("Particles to create");
	ImGui::SetNextItemWidth(ImGui::GetWindowWidth() * 0.3f);
	ImGui::DragInt("##SParticlespercreation", &particlesPerCreation, 1.0f, 1.0f, 500.0f);

	//Particles lifetime
	ImGui::Text("Particles lifetime (ms)");
	if (lifetimeconstants == 0) {
		if (ImGui::DragInt("##SParticlesLifetime1", &particlesLifeTime, 3.0f, 0.0f, 10000.0f))
		{
			UpdateAllGradients();
		}
	}
	else {
		bool changed = false;
		ImGui::SetNextItemWidth(ImGui::GetWindowWidth() * 0.3f);
		if (ImGui::DragInt("##SParticlesLifetime1", &particlesLifeTime1, 3.0f, 0.0f, 10000.0f)) {
			changed = true;
		}
		ImGui::SameLine();
		ImGui::SetNextItemWidth(ImGui::GetWindowWidth() * 0.3f);
		if (ImGui::DragInt("##SParticlesLifetime2", &particlesLifeTime2, 3.0f, 0.0, 10000.0f)) {
			changed = true;
		}
		if (changed) {
			particlesLifeTime = (particlesLifeTime1 < particlesLifeTime2) ? GetRandomValue(particlesLifeTime1, particlesLifeTime2) : GetRandomValue(particlesLifeTime2, particlesLifeTime1);
			UpdateAllGradients();
		}
	}

	ImGui::SameLine();
	if (ImGui::SmallButton("v"))
		ImGui::OpenPopup("Lifetime options");

	if (ImGui::BeginPopup("Lifetime options"))
	{
		if (ImGui::MenuItem("Constant", "", lifetimeconstants == 0 ? true : false))
		{
			lifetimeconstants = 0;
		}
		if (ImGui::MenuItem("Random Between two Constants", "", lifetimeconstants == 1 ? true : false))
		{
			lifetimeconstants = 1;

			particlesLifeTime2 = particlesLifeTime;
			particlesLifeTime1 = particlesLifeTime;
		}
		ImGui::EndPopup();
	}

	int maxParticles = particlesPerCreation / emisionRate * particlesLifeTime;
	ImGui::Text("Total particles alive: %d", maxParticles);

	ImGui::NewLine();
	ImGui::Separator();

	// --- Collisions ---
	if (ImGui::TreeNode("Collision Options"))
	{
		if (ImGui::Checkbox("##PE_EnableColl", &collision_active))
			SetActiveCollisions(collision_active);

		ImGui::SameLine(); ImGui::Text("Enable Collisions");
		ImGui::TreePop();
	}

	ImGui::Separator();
	if (ImGui::TreeNode("Direction & velocity"))
	{
		int cursor = 0;
		ImGui::Text("Particles velocity");
		ImGui::Text("X");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(ImGui::GetWindowWidth() * 0.15f);
		ImGui::DragFloat("##SVelocityX", &particlesVelocity.x, 0.05f, -100.0f, 100.0f);
		ImGui::SameLine();
		ImGui::Text("Y");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(ImGui::GetWindowWidth() * 0.15f);
		ImGui::DragFloat("##SVelocityY", &particlesVelocity.y, 0.05f, -100.0f, 100.0f);
		ImGui::SameLine();
		ImGui::Text("Z");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(ImGui::GetWindowWidth() * 0.15f);
		ImGui::DragFloat("##SVelocityZ", &particlesVelocity.z, 0.05f, -100.0f, 100.0f);
		ImGui::SameLine();
		if (ImGui::SmallButton("v"))
			ImGui::OpenPopup("Velocity options");
		ImGui::Text("Velocity Random Factor");
		cursor = ImGui::GetCursorPosX();
		ImGui::Text("X");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(ImGui::GetWindowWidth() * 0.15f);
		ImGui::DragFloat("##SRandomVelocity1X", &velocityRandomFactor1.x, 0.05f, -100.0f, 100.0f);
		ImGui::SameLine();
		ImGui::Text("Y");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(ImGui::GetWindowWidth() * 0.15f);
		ImGui::DragFloat("##SRandomVelocity1Y", &velocityRandomFactor1.y, 0.05f, -100.0f, 100.0f);
		ImGui::SameLine();
		ImGui::Text("Z");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(ImGui::GetWindowWidth() * 0.15f);
		ImGui::DragFloat("##SRandomVelocity1Z", &velocityRandomFactor1.z, 0.05f, -100.0f, 100.0f);

		ImGui::SetCursorPosX(cursor);
		ImGui::Text(" ");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(ImGui::GetWindowWidth() * 0.15f);
		ImGui::DragFloat("##SRandomVelocity2X", &velocityRandomFactor2.x, 0.05f, -100.0f, 100.0f);
		ImGui::SameLine();
		ImGui::Text(" ");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(ImGui::GetWindowWidth() * 0.15f);
		ImGui::DragFloat("##SRandomVelocity2Y", &velocityRandomFactor2.y, 0.05f, -100.0f, 100.0f);
		ImGui::SameLine();
		ImGui::Text(" ");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(ImGui::GetWindowWidth() * 0.15f);
		ImGui::DragFloat("##SRandomVelocity2Z", &velocityRandomFactor2.z, 0.05f, -100.0f, 100.0f);


		ImGui::TreePop();
	}

	ImGui::Separator();

	if (ImGui::TreeNode("Color over Lifetime"))
	{
		////Particles Color
		int delete_color = -1;
		for (int i = 0; i < colors.size(); ++i)
		{
			std::string label = "##PEParticle Color";
			label.append(std::to_string(i));
			if (ImGui::ColorEdit4(label.data(), (float*)&colors[i], ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar))
			{
				UpdateAllGradients();
			}
			if (colors.size() > 1) {
				colorGradient = true;
				ImGui::SameLine();
				ImGui::PushID(std::to_string(i).c_str());
				if (ImGui::Button("x")) {
					delete_color = i;
				}
				ImGui::PopID();
			}
			else {
				colorGradient = false;
			}
		}

		if (delete_color != -1) {
			int i = 0;
			std::vector<float4>::iterator it = colors.begin();
			while (it != colors.end()) {
				if (i == delete_color) {
					it = colors.erase(it);
					std::vector<float4>::iterator g_it = gradients.begin();
					if (i != 0)
						std::advance(g_it, i - 1);
					gradients.erase(g_it);
					UpdateAllGradients();
				}
				else {
					++it;
				}
				i++;
			}
		}

		if (ImGui::Button("Add color"))
		{
			uint index = colors.size() - 1;
			colors.push_back(colors[index]); //Start the new color with tha same the last one had
			colorDuration = particlesLifeTime / (gradients.size() + 1);

			//Update the gradients
			float4 newGradient = (colors[index + 1] - colors[index]) / colorDuration;
			gradients.push_back(newGradient);
			UpdateAllGradients();
		}

		ImGui::TreePop();
	}

	ImGui::Separator();

	if (ImGui::TreeNode("Sprite rotation"))
	{
		ImGui::Text("Separate Axis");
		ImGui::SameLine();
		ImGui::Checkbox("##separateaxis", &separateAxis);

		// -- Initial rotation --
		ImGui::Text("Initial rotation:");
		ImGui::SameLine();
		ImGui::Text("  ");
		ImGui::SameLine();
		if (ImGui::Checkbox("##srandomInitialRotation", &randomInitialRotation))
		{
			maxInitialRotation[0] = minInitialRotation[0];
			maxInitialRotation[1] = minInitialRotation[1];
			maxInitialRotation[2] = minInitialRotation[2];
		}
		ImGui::SameLine();
		ImGui::Text("Random");

		if (!randomInitialRotation)
		{
			if (separateAxis) {
				ImGui::SetNextItemWidth(ImGui::GetWindowWidth() * 0.15f);
				ImGui::DragInt("##SinitialRotation1X", &minInitialRotation[0], 1, -10000, 10000);
				ImGui::SameLine();
				ImGui::SetNextItemWidth(ImGui::GetWindowWidth() * 0.15f);
				ImGui::DragInt("##SinitialRotation1Y", &minInitialRotation[1], 1, -10000, 10000);
				ImGui::SameLine();
			}
			ImGui::SetNextItemWidth(ImGui::GetWindowWidth() * 0.15f);
			ImGui::DragInt("##SinitialRotation1Z", &minInitialRotation[2], 1, -10000, 10000);
		}
		else
		{
			//Min value
			ImGui::Text("Min:");
			ImGui::SameLine();

			if (separateAxis) {
				ImGui::SetNextItemWidth(ImGui::GetWindowWidth() * 0.15f);
				ImGui::DragInt("##SinitialRotation1X", &minInitialRotation[0], 1, -10000, maxInitialRotation[0]);
				ImGui::SameLine();
				ImGui::SetNextItemWidth(ImGui::GetWindowWidth() * 0.15f);
				ImGui::DragInt("##SinitialRotation1Y", &minInitialRotation[1], 1, -10000, maxInitialRotation[1]);
				ImGui::SameLine();
			}
			ImGui::SetNextItemWidth(ImGui::GetWindowWidth() * 0.15f);
			ImGui::DragInt("##SinitialRotation1Z", &minInitialRotation[2], 1, -10000, maxInitialRotation[2]);

			//Max value
			ImGui::Text("Max:");
			ImGui::SameLine();
			if (separateAxis) {
				ImGui::SetNextItemWidth(ImGui::GetWindowWidth() * 0.15f);
				ImGui::DragInt("##SinitialRotation2X", &maxInitialRotation[0], 1, minInitialRotation[0], 10000.0f);
				ImGui::SameLine();
				ImGui::SetNextItemWidth(ImGui::GetWindowWidth() * 0.15f);
				ImGui::DragInt("##SinitialRotation2Y", &maxInitialRotation[1], 1, minInitialRotation[1], 10000.0f);
				ImGui::SameLine();
			}
			ImGui::SetNextItemWidth(ImGui::GetWindowWidth() * 0.15f);
			ImGui::DragInt("##SinitialRotation2Z", &maxInitialRotation[2], 1, minInitialRotation[2], 10000.0f);
		}

		// -- Rotation speed
		if (rotationconstants == 2) {
			rotateCurve->DrawCurveEditor(); //Draw Curve Editor
			ImGui::SameLine();
			if (ImGui::SmallButton("v"))
				ImGui::OpenPopup("Component options");
		}
		else {
			ImGui::Text("Rotation speed:");
			int cursor = ImGui::GetCursorPosX();
			if (!separateAxis) {
				ImGui::SetNextItemWidth(ImGui::GetWindowWidth() * 0.15f);
				ImGui::DragInt("##Z1", &rotationOvertime1[2]);
				if (rotationconstants) {
					ImGui::SameLine();
					ImGui::SetNextItemWidth(ImGui::GetWindowWidth() * 0.15f);
					ImGui::DragInt("##Z2", &rotationOvertime2[2]);
				}
				ImGui::SameLine();
				if (ImGui::SmallButton("v"))
					ImGui::OpenPopup("Component options");
			}
			else {
				ImGui::SetNextItemWidth(ImGui::GetWindowWidth() * 0.15f);
				ImGui::DragInt("##X0", &rotationOvertime1[0], 1, -1000, 1000);
				ImGui::SameLine();
				ImGui::SetNextItemWidth(ImGui::GetWindowWidth() * 0.15f);
				ImGui::DragInt("##Y0", &rotationOvertime1[1], 1, -1000, 1000);
				ImGui::SameLine();
				ImGui::SetNextItemWidth(ImGui::GetWindowWidth() * 0.15f);
				ImGui::DragInt("##Z0", &rotationOvertime1[2], 1, -1000, 1000);
				ImGui::SameLine();
				if (ImGui::SmallButton("v"))
					ImGui::OpenPopup("Component options");
				if (rotationconstants) {
					ImGui::SetCursorPosX(cursor);
					ImGui::SetNextItemWidth(ImGui::GetWindowWidth() * 0.15f);
					ImGui::DragInt("##X1", &rotationOvertime2[0], 1, -1000, 1000);
					ImGui::SameLine();
					ImGui::SetNextItemWidth(ImGui::GetWindowWidth() * 0.15f);
					ImGui::DragInt("##Y1", &rotationOvertime2[1], 1, -1000, 1000);
					ImGui::SameLine();
					ImGui::SetNextItemWidth(ImGui::GetWindowWidth() * 0.15f);
					ImGui::DragInt("##Z1", &rotationOvertime2[2], 1, -1000, 1000);
				}
			}
		}


		if (ImGui::BeginPopup("Component options"))
		{
			if (ImGui::MenuItem("Constant", "", rotationconstants == 0 ? true : false))
			{
				rotationconstants = 0;
			}
			if (ImGui::MenuItem("Random Between two Constants", "", rotationconstants == 1 ? true : false))
			{
				rotationconstants = 1;
			}
			if (ImGui::MenuItem("Curve Editor", "", rotationconstants == 2 ? true : false))
			{
				rotationconstants = 2;
			}
			ImGui::EndPopup();
		}

		ImGui::TreePop();
	}

	ImGui::Separator();

	if (ImGui::TreeNode("Particles Scale"))
	{

		if (scaleconstants == 0)
		{
			ImGui::Text("Scale");
			ImGui::SameLine();
			ImGui::Text("X");
			ImGui::SameLine(); ImGui::SetNextItemWidth(ImGui::GetWindowWidth() * 0.15f);
			ImGui::DragFloat("##SParticlesScaleX", &particlesScale.x, 0.005f, 0.01f, INFINITY);

			ImGui::SameLine();
			ImGui::Text("Y");
			ImGui::SameLine(); ImGui::SetNextItemWidth(ImGui::GetWindowWidth() * 0.15f);
			ImGui::DragFloat("##SParticlesScaleY", &particlesScale.y, 0.005f, 0.01f, INFINITY);

			if (custom_mesh)
			{
				ImGui::SameLine();
				ImGui::Text("Z");
				ImGui::SameLine(); ImGui::SetNextItemWidth(ImGui::GetWindowWidth() * 0.15f);
				ImGui::DragFloat("##SParticlesScaleZ", &particlesScale.z, 0.005f, 0.01f, INFINITY);
			}
		}
		else if (scaleconstants == 1)
		{
			ImGui::Text("Random Between:");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(ImGui::GetWindowWidth() * 0.15f);
			ImGui::DragFloat("##SParticlesRandomScaleX", &particlesScaleRandomFactor1, 0.005f, 0.0f, INFINITY);
			ImGui::SameLine();
			ImGui::SetNextItemWidth(ImGui::GetWindowWidth() * 0.15f);
			ImGui::DragFloat("##SParticlesRandomScaleY", &particlesScaleRandomFactor2, 0.005f, 0.0f, INFINITY);
		}
		else if (scaleconstants == 2) {
			ImGui::Text("Separate Axis");
			ImGui::SameLine();
			ImGui::Checkbox("##separateaxisScale", &separateAxisScale);
			scaleCurve->DrawCurveEditor();

			if (separateAxisScale) {
				scaleCurveY->DrawCurveEditor();
				scaleCurveZ->DrawCurveEditor();
			}
		}

		ImGui::SameLine();
		if (ImGui::SmallButton("v"))
			ImGui::OpenPopup("Component options");

		if (ImGui::BeginPopup("Component options"))
		{
			if (ImGui::MenuItem("Constant", "", scaleconstants == 0 ? true : false))
				scaleconstants = 0;

			if (ImGui::MenuItem("Random Between two Constants", "", scaleconstants == 1 ? true : false))
				scaleconstants = 1;

			if (ImGui::MenuItem("Curve Editor (over lifetime)", "", scaleconstants == 2 ? true : false))
				scaleconstants = 2;

			ImGui::EndPopup();
		}

		ImGui::TreePop();
	}

	ImGui::Separator();

	if (ImGui::TreeNode("Animation"))
	{
		if (!custom_mesh)
		{
			int tmpX = tileSize_X;
			int tmpY = tileSize_Y;
			ImGui::Checkbox("Animation", &animation);
			ImGui::Text("Tiles:");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(ImGui::GetWindowWidth() * 0.15f);
			ImGui::DragInt("X", &tileSize_X, 1, 1, texture->Texture_width);
			ImGui::SameLine();
			ImGui::SetNextItemWidth(ImGui::GetWindowWidth() * 0.15f);
			ImGui::DragInt("Y", &tileSize_Y, 1, 1, texture->Texture_height);
			ImGui::Text("Start Frame:");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(ImGui::GetWindowWidth() * 0.15f);
			ImGui::DragInt("##sframe", &startFrame, 1, 0, (tileSize_X * tileSize_Y) - 1);
			ImGui::SameLine();
			ImGui::Checkbox("##srandomfirstframe", &randomStartFrame);
			ImGui::SameLine();
			ImGui::Text("Random");
			ImGui::Text("Cycles:");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(ImGui::GetWindowWidth() * 0.15f);
			ImGui::DragFloat("##cycle", &cycles, 0.01, 0.01, 100);

			if (cycles <= 0)
				cycles = 1;
			if (tmpX != tileSize_X || tmpY != tileSize_Y && animation)
				createdAnim = false;
		}
		else
		{
			ImGui::NewLine(); ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x + 10.0f);
			ImGui::Text("Cannot put an Animation if Particles have a Custom Mesh!");
			ImGui::NewLine(); ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x + 10.0f);
			ImGui::Text("You have to use Default Mesh");
		}

		ImGui::TreePop();
	}

	ImGui::Separator();

	if (ImGui::TreeNode("Renderer"))
	{
		// Shadows & Lighting
		ImGui::NewLine();
		ImGui::NewLine(); ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x + 10.0f);
		ImGui::Checkbox("Light Affected ", &m_AffectedByLight);
		ImGui::SameLine();
		ImGui::Checkbox("Scene Color Affected", &m_AffectedBySceneColor);

		ImGui::NewLine(); ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x + 10.0f);
		if (ImGui::Checkbox("Cast Shadows", &m_CastShadows))
			if (m_OnlyShadows) m_CastShadows = false;

		ImGui::SameLine();
		ImGui::NewLine(); ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x + 17.0f);
		if (ImGui::Checkbox("Receive Shadows", &m_ReceiveShadows))
			if (m_OnlyShadows) m_ReceiveShadows = false;

		ImGui::SameLine();
		if (ImGui::Checkbox("Only Shadows", &m_OnlyShadows))
		{
			m_ReceiveShadows = false;
			m_CastShadows = true;
		}

		// --- Particles Mesh ---
		ImGui::NewLine();
		ImGui::NewLine(); ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x + 10.0f);

		if (particles_mesh)
			ImGui::ImageButton((void*)(uint)particles_mesh->GetPreviewTexID(), ImVec2(20, 20));
		else
			ImGui::ImageButton(NULL, ImVec2(20, 20), ImVec2(0, 0), ImVec2(1, 1), 2);

		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("GO"))
			{
				uint UID = *(const uint*)payload->Data;
				GameObject* gobj = App->scene_manager->currentScene->GetGOWithUID(UID);
				if (gobj)
				{
					ComponentMesh* meshComp = gobj->GetComponent<ComponentMesh>();
					if (meshComp)
					{
						ResourceMesh* mesh = meshComp->resource_mesh;
						if (particles_mesh && particles_mesh->GetUID() != App->scene_manager->plane->GetUID())
						{
							particles_mesh->RemoveUser(GO);
							particles_mesh->Release();
						}

						particles_mesh = (ResourceMesh*)App->resources->GetResource(mesh->GetUID());
						particles_mesh->AddUser(GO);
						animation = createdAnim = false;
						custom_mesh = true;
					}
					else
						ENGINE_CONSOLE_LOG("GameObject dropped has no Mesh component!");
				}
				else
					ENGINE_CONSOLE_LOG("The element dropped was not a GameObject!");
			}

			ImGui::EndDragDropTarget();
		}

		ImGui::SameLine(); ImGui::Text("Particles Mesh");
		ImGui::NewLine(); ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x + 10.0f);

		if (ImGui::Button("Default", { 77, 18 }))
		{
			if (particles_mesh)
			{
				if (particles_mesh->GetUID() != App->scene_manager->plane->GetUID())
				{
					particles_mesh->RemoveUser(GO);
					particles_mesh->Release();
				}
			}

			particlesScale.z = 1.0f;
			particles_mesh = App->scene_manager->plane;
			//particles_mesh->AddUser(GO);
			custom_mesh = false;
		}

		// --- Image/Texture ---
		ImGui::NewLine();
		ImGui::NewLine(); ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x + 10.0f);
		ImGui::Text("Texture");

		ImGui::NewLine(); ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x + 10.0f);
		if (texture == nullptr)
			ImGui::Image((ImTextureID)App->textures->GetDefaultTextureID(), ImVec2(100, 100), ImVec2(0, 1), ImVec2(1, 0)); //default texture
		else
			ImGui::Image((ImTextureID)texture->GetTexID(), ImVec2(100, 100), ImVec2(0, 1), ImVec2(1, 0)); //loaded texture

		//drag and drop
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("resource"))
			{
				uint UID = *(const uint*)payload->Data;
				Resource* resource = App->resources->GetResource(UID, false);

				if (resource && resource->GetType() == Resource::ResourceType::TEXTURE)
				{
					if (texture)
					{
						texture->RemoveUser(GO);
						texture->Release();
					}

					texture = (ResourceTexture*)App->resources->GetResource(UID);

					if (texture)
						texture->AddUser(GO);
				}
			}
			ImGui::EndDragDropTarget();
		}

		//Unuse Texture
		ImGui::SameLine();
		if (ImGui::Button("Unuse", { 77, 18 }) && texture)
		{
			if (GO)
				texture->RemoveUser(GO);

			texture->Release();
			texture = nullptr;
		}

		// --- Color ---
		ImGui::NewLine(); ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x + 10.0f);
		ImGui::ColorEdit4("##PEParticle Color", (float*)&colors[0], ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
		ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
		ImGui::Text("Start Color");

		// --- Face Culling ---
		ImGui::NewLine(); ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x + 10.0f);
		ImGui::Checkbox("##PE_PartsCullF", &particlesFaceCulling);
		ImGui::SameLine();
		ImGui::Text("Particles Face Culling");

		// --- Billboarding Type ---
		ImGui::NewLine(); ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x + 10.0f);
		if (ImGui::Checkbox("##PEBill", &particlesBillboarding))
			if(!particlesBillboarding)
				horizontalBillboarding = verticalBillboarding = false;

		ImGui::SameLine();
		ImGui::Text("Particles Billboarding");

		if (particlesBillboarding)
		{
			ImGui::NewLine(); ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x + 37.0f);
			if (ImGui::Checkbox("##PEHBill", &horizontalBillboarding))
				if (horizontalBillboarding && verticalBillboarding)
					verticalBillboarding = false;

			ImGui::SameLine();
			ImGui::Text("Horizontal Billboarding");

			ImGui::NewLine(); ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x + 37.0f);
			if (ImGui::Checkbox("##PEVBill", &verticalBillboarding))
				if (verticalBillboarding && horizontalBillboarding)
					horizontalBillboarding = false;

			ImGui::SameLine();
			ImGui::Text("Vertical Billboarding");
		}

		// --- Tree Node for Blending ---
		ImGui::NewLine();
		ImGui::NewLine(); ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x + 10.0f);
		if (ImGui::TreeNode("Particle Emitter Blending"))
		{
			HandleEditorBlendingSelector();
			ImGui::TreePop();
		}

		ImGui::TreePop();
	}
}

void ComponentParticleEmitter::HandleEditorBlendingSelector()
{
	ImGui::NewLine();
	ImGui::NewLine(); ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x + 20.0f);
	ImGui::Text("Blend Equation");
	ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x + 10.0f);
	ImGui::SetNextItemWidth(200.0f);

	// --- Blend Eq ---
	std::vector<const char*> blendEq = App->renderer3D->m_BlendEquationFunctionsVec;
	int index = (int)m_PartBlEquation;
	if (App->gui->HandleDropdownSelector(index, "##PAlphaEq", blendEq.data(), blendEq.size()))
		m_PartBlEquation = (BlendingEquations)index;

	// --- Blend Auto Func ---
	ImGui::NewLine(); ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x + 20.0f);
	ImGui::Text("Blend Mode"); ImGui::SameLine();
	ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x + 38.0f);
	ImGui::SetNextItemWidth(200.0f);

	std::vector<const char*> blendAutoF_Vec = App->renderer3D->m_BlendAutoFunctionsVec;
	int index1 = (int)m_PartBlendFunc;
	if (App->gui->HandleDropdownSelector(index1, "##PAlphaAutoFunction", blendAutoF_Vec.data(), blendAutoF_Vec.size()))
		m_PartBlendFunc = (BlendAutoFunction)index1;

	//Help Marker
	std::string desc = "Stand. = SRC, 1-SRCALPH\nAdd. = ONE, ONE\nAddAlph. = SRC_ALPH, ONE\nMult. = DSTCOL, ZERO";
	ImGui::SameLine();
	App->gui->HelpMarker(desc.c_str());

	// --- Blend Manual Function ---
	ImGui::NewLine(); ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x + 20.0f);
	ImGui::Checkbox("Auto Alpha", &m_PartAutoBlending);
	if (!m_PartAutoBlending)
	{
		//ImGui::Separator();
		//ImGui::NewLine();
		ImGui::NewLine(); ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x + 20.0f);
		if (ImGui::TreeNodeEx("Manual Alpha", ImGuiTreeNodeFlags_DefaultOpen))
		{
			//Source
			//ImGui::NewLine();
			ImGui::NewLine(); ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x + 30.0f);
			ImGui::Text("Source Alpha"); ImGui::SameLine();
			ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x + 45.0f);
			ImGui::SetNextItemWidth(200.0f);

			std::vector<const char*> blendTypes_Vec = App->renderer3D->m_AlphaTypesVec;
			int index2 = (int)m_MPartBlend_Src;
			if (App->gui->HandleDropdownSelector(index2, "##PManualAlphaSrc", blendTypes_Vec.data(), blendTypes_Vec.size()))
				m_MPartBlend_Src = (BlendingTypes)index2;

			//Destination
			ImGui::NewLine(); ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x + 30.0f);
			ImGui::Text("Destination Alpha"); ImGui::SameLine();
			ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x + 10.0f);
			ImGui::SetNextItemWidth(200.0f);

			int index3 = (int)m_MPartBlend_Dst;
			if (App->gui->HandleDropdownSelector(index3, "##PManualAlphaDst", blendTypes_Vec.data(), blendTypes_Vec.size()))
				m_MPartBlend_Dst = (BlendingTypes)index3;

			//Reference Function
			ImGui::NewLine(); ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x + 30.0f);
			if (ImGui::Button("Reference (Test Blend)", { 180, 18 })) App->gui->RequestBrowser("https://www.andersriggelsen.dk/glblendfunc.php");
			ImGui::TreePop();
		}
	}
}



double ComponentParticleEmitter::GetRandomValue(double min, double max) //EREASE IN THE FUTURE
{
	return App->RandomNumberGenerator.GetDoubleRNinRange(min, max);
}

void ComponentParticleEmitter::SetActiveCollisions(bool collisionsActive)
{
	if (collisionsActive){
		physx::PxFilterData filterData;
		filterData.word0 = (1 << GO->layer);
		filterData.word1 = App->physics->layer_list.at(GO->layer).LayerGroup;
		particleSystem->setSimulationFilterData(filterData);
	}
	else{
		physx::PxFilterData filterData;
		filterData.word0 = 0;
		filterData.word1 = 0;
		particleSystem->setSimulationFilterData(filterData);

	}
}

void ComponentParticleEmitter::CreateParticles(uint particlesAmount)
{
	uint particlesToCreate = particlesAmount;

	if (validParticles < maxParticles)
	{
		if (particlesToCreate > maxParticles - validParticles)
			particlesToCreate = maxParticles - validParticles;

		Quat totalRotation = Quat::identity;
		Quat externalRotation = Quat::identity;
		float3 globalPosition = float3::zero;

		Quat globalRotation;
		float3 scale_, position_;

		switch (rotationType)
		{
		case Broken::ROTATION_PARENT::GO_LOCAL:
			totalRotation = GO->GetComponent<ComponentTransform>()->rotation * emitterRotation;
			externalRotation = GO->GetComponent<ComponentTransform>()->rotation;
			break;
		case Broken::ROTATION_PARENT::GO_GLOBAL:
			GO->GetComponent<ComponentTransform>()->GetGlobalTransform().Decompose(position_, globalRotation, scale_);
			totalRotation = globalRotation * emitterRotation;
			externalRotation = globalRotation;
			break;
		case Broken::ROTATION_PARENT::NONE:
			totalRotation = emitterRotation;
			break;
		}

		globalPosition = GO->GetComponent<ComponentTransform>()->GetGlobalPosition();

		validParticles += particlesToCreate;
		spawnClock = App->time->GetGameplayTimePassed() * 1000;

		physx::PxParticleCreationData creationData;

		//Create necessary amount of particles
		creationData.numParticles = particlesToCreate;

		//Create indices and allocate them
		physx::PxU32* index = new physx::PxU32[particlesToCreate];
		const physx::PxStrideIterator<physx::PxU32> indexBuffer(index);
		indexPool->allocateIndices(particlesToCreate, indexBuffer);


		physx::PxVec3* positionBuffer = new physx::PxVec3[particlesToCreate];
		physx::PxVec3* velocityBuffer = new physx::PxVec3[particlesToCreate];

		for (int i = 0; i < particlesToCreate; ++i) {

			//Set velocity of the new particles
			physx::PxVec3 velocity = physx::PxVec3(
				particlesVelocity.x + ((velocityRandomFactor1.x < velocityRandomFactor2.x) ? GetRandomValue(velocityRandomFactor1.x, velocityRandomFactor2.x) : GetRandomValue(velocityRandomFactor2.x, velocityRandomFactor1.x)),
				particlesVelocity.y + ((velocityRandomFactor1.y < velocityRandomFactor2.y) ? GetRandomValue(velocityRandomFactor1.y, velocityRandomFactor2.y) : GetRandomValue(velocityRandomFactor2.y, velocityRandomFactor1.y)),
				particlesVelocity.z + ((velocityRandomFactor1.z < velocityRandomFactor2.z) ? GetRandomValue(velocityRandomFactor1.z, velocityRandomFactor2.z) : GetRandomValue(velocityRandomFactor2.z, velocityRandomFactor1.z)));

			Quat velocityQuat = Quat(velocity.x, velocity.y, velocity.z, 0);
			velocityQuat = totalRotation * velocityQuat * totalRotation.Conjugated();
			velocityBuffer[i] = physx::PxVec3(velocityQuat.x, velocityQuat.y, velocityQuat.z);

			/*The spawn position of the particle is a combination of different variables (size, emmitter position and global position).
			Each are affected by different rotations:
				- positionFromSize is affected by totalRotation (globalRotation * emitterRotation)
				- positionFromEmitterPos is only affected by externalRotation (rotation of the GO)
				- globalPosition is not affected by these rotations (only affected by the rotations of the parents of the GO*/

			//Set positionFromSize of the new particles
			physx::PxVec3 positionFromSize(GetRandomValue(-size.x, size.x),
				+GetRandomValue(-size.y, size.y),
				+GetRandomValue(-size.z, size.z));


			Quat positionFromSizeQuat = Quat(positionFromSize.x, positionFromSize.y, positionFromSize.z, 0);
			positionFromSizeQuat = totalRotation * positionFromSizeQuat * totalRotation.Conjugated();

			//Set positionFromEmitterPos
			physx::PxVec3 positionFromEmitterPos(emitterPosition.x,emitterPosition.y,emitterPosition.z);

			Quat positionFromEmitterPosQuat = Quat(positionFromEmitterPos.x, positionFromEmitterPos.y, positionFromEmitterPos.z, 0);
			positionFromEmitterPosQuat = externalRotation * positionFromEmitterPosQuat * externalRotation.Conjugated();

			//Assign final position to the particle
			positionBuffer[i] =  physx::PxVec3(	positionFromSizeQuat.x + positionFromEmitterPosQuat.x + globalPosition.x,
												positionFromSizeQuat.y + positionFromEmitterPosQuat.y + globalPosition.y,
												positionFromSizeQuat.z + positionFromEmitterPosQuat.z + globalPosition.z);

			//Aditional properties
			particles[index[i]]->lifeTime = particlesLifeTime;
			particles[index[i]]->spawnTime = spawnClock;
			particles[index[i]]->color = colors[0];
			particles[index[i]]->texture = texture;
			particles[index[i]]->gradientTimer = spawnClock;
			particles[index[i]]->currentGradient = 0;
			particles[index[i]]->emitterSpawnPosition = globalPosition;
			particles[index[i]]->startFrame = randomStartFrame ? GetRandomValue(0, double(tileSize_X)*double(tileSize_Y)): startFrame;
			particles[index[i]]->h_billboard = horizontalBillboarding;
			particles[index[i]]->v_billboard = verticalBillboarding;
			particles[index[i]]->cam_billboard = particlesBillboarding;
			particles[index[i]]->scene_colorAffected = m_AffectedBySceneColor;
			particles[index[i]]->light_Affected = m_AffectedByLight;
			particles[index[i]]->receive_shadows = m_ReceiveShadows;
			particles[index[i]]->intialRotation = externalRotation;

			//Set scale
			if (scaleconstants == 1)
				particles[index[i]]->scale = particlesScale * GetRandomValue(particlesScaleRandomFactor1, particlesScaleRandomFactor2);
			else if (scaleconstants == 0)
				particles[index[i]]->scale = particlesScale;

			if (!custom_mesh)
				particles[index[i]]->scale.z = particlesScale.z = 1.0f;

			// -- Rotation --
			//Initial rotation
			if (randomInitialRotation)
			{
				if (separateAxis)
				{
					particles[index[i]]->rotation = float3(	GetRandomValue(minInitialRotation[0], maxInitialRotation[0]) * DEGTORAD,
															GetRandomValue(minInitialRotation[1], maxInitialRotation[1]) * DEGTORAD,
															GetRandomValue(minInitialRotation[2], maxInitialRotation[2]) * DEGTORAD);
				}
				else
					particles[index[i]]->rotation = float3(0.0f, 0.0f, GetRandomValue(minInitialRotation[2], maxInitialRotation[2]) * DEGTORAD);
			}
			else
			{
				if (separateAxis)
					particles[index[i]]->rotation = float3(minInitialRotation[0] * DEGTORAD, minInitialRotation[1] * DEGTORAD, minInitialRotation[2] * DEGTORAD);
				else
					particles[index[i]]->rotation = float3(	0.0f, 0.0f, minInitialRotation[2] * DEGTORAD);
			}

			//Rotation velocity
			float3 rot1 = float3::zero;
			float3 rot2 = float3::zero;

			if (separateAxis)
			{
				rot1 = float3((float)rotationOvertime1[0], (float)rotationOvertime1[1], (float)rotationOvertime1[2]);
				rot2 = float3((float)rotationOvertime2[0], (float)rotationOvertime2[1], (float)rotationOvertime2[2]);
			}
			else
			{
				rot1 = float3(0, 0, (float)rotationOvertime1[2]);
				rot2 = float3(0, 0, (float)rotationOvertime2[2]);
			}

			if (rotationconstants == 0)
				particles[index[i]]->rotationSpeed = rot1;

			if (rotationconstants == 1)
			{
				if (rot1.x <= rot2.x)
					particles[index[i]]->rotationSpeed.x = GetRandomValue(rot1.x, rot2.x);
				else
					particles[index[i]]->rotationSpeed.x = GetRandomValue(rot2.x, rot1.x);

				if (rot1.y <= rot2.y)
					particles[index[i]]->rotationSpeed.y = GetRandomValue(rot1.y, rot2.y);
				else
					particles[index[i]]->rotationSpeed.y = GetRandomValue(rot2.y, rot1.y);

				if (rot1.z <= rot2.z)
					particles[index[i]]->rotationSpeed.z = GetRandomValue(rot1.z, rot2.z);
				else
					particles[index[i]]->rotationSpeed.z = GetRandomValue(rot2.z, rot1.z);
			}

		}

		creationData.indexBuffer = indexBuffer;
		creationData.positionBuffer = physx::PxStrideIterator<const physx::PxVec3>(positionBuffer);
		creationData.velocityBuffer = physx::PxStrideIterator<const physx::PxVec3>(velocityBuffer);

		bool succes = particleSystem->createParticles(creationData);

		delete[] index;
		delete[] positionBuffer;
		delete[] velocityBuffer;
	}
}

void ComponentParticleEmitter::CreateAnimation(uint w, uint h)
{
	int width = texture->Texture_width / w;
	int height = texture->Texture_height / h;

	for (int j = h - 1; j >= 0; --j) {
		for (int i = 0; i < w; ++i) {
			//Create new Frame
			ResourceMesh* plane = new ResourceMesh(App->GetRandom().Int(), "ParticleMesh");
			App->scene_manager->CreatePlane(1, 1, 1, plane);

			//Set Texture Coords
			plane->vertices[0].texCoord[0] = i * width / (float)texture->Texture_width;
			plane->vertices[0].texCoord[1] = j * height / (float)texture->Texture_height;
			plane->vertices[2].texCoord[0] = ((i * width) + width) / (float)texture->Texture_width;
			plane->vertices[2].texCoord[1] = j * height / (float)texture->Texture_height;
			plane->vertices[1].texCoord[0] = i * width / (float)texture->Texture_width;
			plane->vertices[1].texCoord[1] = ((j * height) + height) / (float)texture->Texture_height;
			plane->vertices[3].texCoord[0] = ((i * width) + width) / (float)texture->Texture_width;
			plane->vertices[3].texCoord[1] = ((j * height) + height) / (float)texture->Texture_height;

			//Update Buffer
			plane->LoadInMemory();
			particleMeshes.push_back(plane);
		}
	}
}

void ComponentParticleEmitter::UpdateAllGradients()
{
	if (gradients.size() > 0) {
		colorDuration = particlesLifeTime / gradients.size();
		for (int i = 0; i < colors.size(); ++i) {
			if (i == 0) //If we change the first color, only 1 gradient is affected
			{
				gradients[0] = (colors[1] - colors[0]) / colorDuration;
			}
			else if (i == colors.size() - 1) //If we changed the last color, only 1 gradient is affected
			{
				gradients[i - 1] = (colors[i] - colors[i - 1]) / colorDuration;
			}
			else //Else, 2 gradients are afected
			{
				gradients[i - 1] = (colors[i] - colors[i - 1]) / colorDuration;
				gradients[i] = (colors[i + 1] - colors[i]) / colorDuration;
			}
		}
	}
}

void ComponentParticleEmitter::UpdateAABBs()
{
	Quat totalRotation = Quat::identity;
	Quat externalRotation = Quat::identity;
	float3 globalPosition = GO->GetComponent<ComponentTransform>()->GetGlobalPosition();

	Quat globalRotation;
	float3 scale_, position_;

	switch (rotationType)
	{
	case Broken::ROTATION_PARENT::GO_GLOBAL:
		GO->GetComponent<ComponentTransform>()->GetGlobalTransform().Decompose(position_, globalRotation, scale_);
		totalRotation = globalRotation * emitterRotation;
		externalRotation = globalRotation;
		break;
	case Broken::ROTATION_PARENT::NONE:
		totalRotation = emitterRotation;
		break;
	}

	AABB aabb(float3(-size.x, -size.y, -size.z), float3(size.x, size.y, size.z));

	emisionAreaOBB.SetFrom(aabb, totalRotation);

	Quat positionFromEmitterPosQuat(emitterPosition.x, emitterPosition.y, emitterPosition.z, 0);
	positionFromEmitterPosQuat = externalRotation * positionFromEmitterPosQuat * externalRotation.Conjugated();

	emisionAreaOBB.pos = emisionAreaOBB.pos + float3(positionFromEmitterPosQuat.x + globalPosition.x, positionFromEmitterPosQuat.y + globalPosition.y, positionFromEmitterPosQuat.z + globalPosition.z);

	particlesAreaAABB.SetFrom(emisionAreaOBB);

	// -- Calculate MAX AABB point --
	Quat maxVel = Quat(particlesVelocity.x + velocityRandomFactor1.x,
		particlesVelocity.y + velocityRandomFactor1.y,
		particlesVelocity.z + velocityRandomFactor1.z,
		0);

	maxVel = totalRotation * maxVel * totalRotation.Conjugated();

	float3 newPoint = particlesAreaAABB.maxPoint;
	newPoint.x += maxVel.x * (float(particlesLifeTime) / 1000) * 2.0f;
	newPoint.y += maxVel.y * (float(particlesLifeTime) / 1000) * 2.0f;
	newPoint.z += maxVel.z * (float(particlesLifeTime) / 1000) * 2.0f;

	particlesAreaAABB.Enclose(newPoint);

	newPoint.x +=  0.5 * externalAcceleration.x * (((float(particlesLifeTime) / 1000)) * (float(particlesLifeTime) / 1000));
	newPoint.y +=  0.5 * (externalAcceleration.y-10.0f) * (((float(particlesLifeTime) / 1000)) * (float(particlesLifeTime) / 1000));
	newPoint.z += 0.5 * externalAcceleration.z * (((float(particlesLifeTime) / 1000)) * (float(particlesLifeTime) / 1000));

	particlesAreaAABB.Enclose(newPoint);

	// -- Calculate MIN AABB point --

	Quat minVel = Quat(particlesVelocity.x + velocityRandomFactor2.x,
		particlesVelocity.y + velocityRandomFactor2.y,
		particlesVelocity.z + velocityRandomFactor2.z,
		0);

	minVel = totalRotation * minVel * totalRotation.Conjugated();

	newPoint = particlesAreaAABB.minPoint;
	newPoint.x += minVel.x * (float(particlesLifeTime) / 1000) * 2.0f;
	newPoint.y += minVel.y * (float(particlesLifeTime) / 1000) * 2.0f;
	newPoint.z += minVel.z * (float(particlesLifeTime) / 1000) * 2.0f;

	particlesAreaAABB.Enclose(newPoint);
	newPoint.x += 0.5 * externalAcceleration.x * (((float(particlesLifeTime) / 1000.0f)) * (float(particlesLifeTime) / 1000));
	newPoint.y += 0.5 * (externalAcceleration.y - 10.0f) * (((float(particlesLifeTime) / 1000)) * (float(particlesLifeTime) / 1000));
	newPoint.z += 0.5 * externalAcceleration.z * (((float(particlesLifeTime) / 1000)) * (float(particlesLifeTime) / 1000));

	particlesAreaAABB.Enclose(newPoint);

}

void ComponentParticleEmitter::DrawEmitterArea()
{
	UpdateAABBs();

	App->renderer3D->DrawOBB(emisionAreaOBB, Blue);
	App->renderer3D->DrawAABB(particlesAreaAABB,Green);
}

void ComponentParticleEmitter::Play()
{
	emisionActive = true;
	emisionStart = App->time->GetGameplayTimePassed() * 1000;
	spawnClock = emisionStart;
	firstEmision = true;
	playNow = true;
}

void ComponentParticleEmitter::Stop()
{
	emisionActive = false;
}

void ComponentParticleEmitter::SetLooping(bool active)
{
	loop = active;
	firstEmision = true;
}

void ComponentParticleEmitter::SetEmisionRate(float ms)
{
	emisionRate = ms;
}

void ComponentParticleEmitter::SetParticlesPerCreation(int particlesAmount)
{
	particlesPerCreation = particlesAmount;
}

void ComponentParticleEmitter::SetExternalAcceleration(float x, float y, float z)
{
	particleSystem->setExternalAcceleration(physx::PxVec3(x, y, z));
}

void ComponentParticleEmitter::SetParticlesVelocity(float x, float y, float z)
{
	particlesVelocity = physx::PxVec3(x, y, z);
}

void ComponentParticleEmitter::SetVelocityRF(float3 rand1, float3 rand2)
{
	velocityRandomFactor1 = physx::PxVec3(rand1.x, rand1.y, rand1.z);
	velocityRandomFactor2 = physx::PxVec3(rand2.x, rand2.y, rand2.z);
}

void ComponentParticleEmitter::SetDuration(int _duration)
{
	this->duration = duration;
}

void ComponentParticleEmitter::SetLifeTime(int ms)
{
	particlesLifeTime = ms;

	colorDuration = particlesLifeTime / gradients.size();

	//Update gradients if we have to
	if (colors.size() > 1)
	{
		UpdateAllGradients();
	}
}

void ComponentParticleEmitter::SetParticlesScale(float x, float y, float z)
{
	if (custom_mesh)
		particlesScale = { x, y, z };
	else
		particlesScale = { x, y, 1.0f };
}

void ComponentParticleEmitter::SetParticlesScaleRF(float randomFactor1, float randomFactor2)
{
	particlesScaleRandomFactor1 = randomFactor1;
	particlesScaleRandomFactor2 = randomFactor2;
}

void ComponentParticleEmitter::UpdateActorLayer(const int* layerMask)
{
	App->physics->UpdateParticleActorLayer(particleSystem, (LayerMask*)layerMask);
}

void ComponentParticleEmitter::SetOffsetPosition(float x, float y, float z)
{
	emitterPosition = float3(x, y, z);
}

void ComponentParticleEmitter::SetOffsetRotation(float x, float y, float z)
{
	float3 rotation(x, y, z);
	float3 difference = (rotation - eulerRotation) * DEGTORAD;
	Quat quatrot = Quat::FromEulerXYZ(difference.x, difference.y, difference.z);

	emitterRotation = Quat::FromEulerXYZ(rotation.x * DEGTORAD, rotation.y * DEGTORAD, rotation.z * DEGTORAD);
	eulerRotation = rotation;
}

void ComponentParticleEmitter::SetScaleOverTime(float scale)
{
	scaleOverTime = scale;
}

void ComponentParticleEmitter::SetTexture(uint UID)
{
	Resource* resource = App->resources->GetResource(UID, false);

	if (resource && resource->GetType() == Resource::ResourceType::TEXTURE)
	{
		if (texture)
			texture->Release();

		texture = (ResourceTexture*)App->resources->GetResource(UID);
	}
	else
		ENGINE_CONSOLE_LOG("!(Particles - Set Texture): Couldn't find texture or was invalid to put in Particles");
}


void ComponentParticleEmitter::SetParticlesRotationOverTime(int rotationOverTime)
{
	separateAxis = false;
	rotationOvertime1[2] = rotationOverTime;
}

void ComponentParticleEmitter::SetParticlesRandomRotationOverTime(int randomRotation)
{
	rotationconstants = true;
	separateAxis = false;
	rotationOvertime2[2] = randomRotation;
}

void ComponentParticleEmitter::SetParticles3DRotationOverTime(int rotationOverTimeX, int rotationOverTimeY, int rotationOverTimeZ)
{
	separateAxis = true;
	rotationOvertime1[0] = rotationOverTimeX;
	rotationOvertime1[1] = rotationOverTimeY;
	rotationOvertime1[2] = rotationOverTimeZ;
}

void ComponentParticleEmitter::SetParticles3DRandomRotationOverTime(int rotationOverTimeX, int rotationOverTimeY, int rotationOverTimeZ)
{
	rotationconstants = true;
	separateAxis = true;
	rotationOvertime2[0] = rotationOverTimeX;
	rotationOvertime2[1] = rotationOverTimeY;
	rotationOvertime2[2] = rotationOverTimeZ;
}

void ComponentParticleEmitter::RemoveParticlesRandomRotation()
{
	rotationconstants = false;
}
