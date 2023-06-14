#ifndef GLTFSCENEEENCODER_H_
#define GLTFSCENEEENCODER_H_


#include <iostream>
#include <list>
#include <vector>
#include <ctime>

#include "Base.h"
#include "StringUtil.h"
#include "Object.h"
#include "Node.h"
#include "Camera.h"
#include "Light.h"
#include "Mesh.h"
#include "MeshPart.h"
#include "MeshSkin.h"
#include "Model.h"
#include "Scene.h"
#include "Animation.h"
#include "AnimationChannel.h"
#include "Vertex.h"
#include "Matrix.h"
#include "Transform.h"
#include "GPBFile.h"
#include "EncoderArguments.h"

#include "tiny_gltf.h"


using namespace gameplay;

class GLTFSceneEncoder
{
public:
	GLTFSceneEncoder();

	~GLTFSceneEncoder();

	/**
	 * Writes out encoded GLTF file.
	 */
	void write(const std::string& filepath, const EncoderArguments& arguments);



private:
	/**
	 * Loads the scene.
	 *
	 * @param gltfScene The gltf scene to load.
	 */
	Scene* createScene(const tinygltf::Scene* gltfScene);
	
	Node* getOrCreateNode(const tinygltf::Node* gltfNode,const std::string& gltfID);

	void setTransform(const tinygltf::Node* gltfNode, Node* gameNode);

	bool setCameraComponent(const tinygltf::Node* gltfNode, Node* gameNode);

	bool setLightComponent(const tinygltf::Node* gltfNode, Node* gameNode);
	
	bool setModelComponent(const tinygltf::Node* gltfNode, Node* gameNode);

	Material* getOrCreateMaterial(const tinygltf::Material* gltfMat, const std::string& gltfID, bool isLit,bool hasVertexColor);

	Mesh* getOrCreateMesh(const tinygltf::Mesh* gltfMesh, const std::string& gltfID);

	std::vector<Vertex> loadVertexData(const tinygltf::Mesh* gltfMesh, std::vector<int>& lengths);

	void loadIndexData(const tinygltf::Primitive* gltfSubmesh, std::vector<int>& indexData);

	bool setSkinComponent(const tinygltf::Mesh* gltfMesh, Model* gameModel);

	void setMaterialTextures(const tinygltf::Material* gltfMat, Material* gameMat);

	void setMaterialUniforms(const tinygltf::Material* gltfMat, Material* gameMat);

	Material* findBaseMaterial(Material* gameMat);

	Material* createBaseMaterial(const std::string& baseMaterialName, Material* childMaterial);
	/**
	 * Writes a material file.
	 *
	 * @param filepath
	 *
	 * @return True if successful; false otherwise.
	 */
	bool writeMaterial(const std::string& filepath);

	void loadAnimations(const std::vector<tinygltf::Animation>& gltfAnimas);

	void loadAnimation(const tinygltf::Animation& gltfAnima,const std::string& animaName);

	struct SRTChannel
	{
		const tinygltf::AnimationChannel* scaleChannel{ nullptr };
		const tinygltf::AnimationChannel* rotationChannel{ nullptr };
		const tinygltf::AnimationChannel* translationChannel{ nullptr };
		const tinygltf::Animation* animation{ nullptr };

		float startTime{0};
		float stopTime{0};

		unsigned getTargetAttribute();

		void sampleToGameAnimationChannel(AnimationChannel* gameAnimaChannel, GLTFSceneEncoder* encoder);
	};

	std::map<tinygltf::Node*, SRTChannel> collectSRTChannel(const tinygltf::Animation* gltfAnima);

	void calculateLerpFactor(const tinygltf::Accessor* accessor, const tinygltf::BufferView* bufferView, const tinygltf::Buffer* buffer, float time, int& i0, int& i1, float& f);

	Vector3 sampleVector3(const tinygltf::AnimationSampler* sampler, float time);

	Quaternion sampleQuat(const tinygltf::AnimationSampler* sampler, float time);
private:

	friend SRTChannel;

	/**
 * The GamePlay file that is populated while reading the FBX file.
 */
	GPBFile m_gamePlayFile;
	tinygltf::Model m_contexModel;

	std::map<tinygltf::Material*, Material*> m_materialsMapper;
	std::map<tinygltf::Mesh*, Mesh*> m_meshMapper;
	std::map<tinygltf::Node*, Node*> m_nodeMapper;

	std::map<tinygltf::Camera*, Camera*> m_cameraMapper;
	std::map<tinygltf::Light*, Light*> m_lightMapper;
	std::map<std::string, Material*> m_baseMaterialMapper;

	template<typename K,typename V>
	bool tryFindValue(const std::map<K,V>& map,K key,V& outValue)
	{
		auto itr = map.find(key);
		if(itr == map.end())
		{
			return false;
		}
		outValue = itr->second;
		return true;
	}

	template<typename K, typename V>
	bool tryFindKey(const std::map<K, V>& map, V value, K& outKey)
	{
		for (auto kv:map)
		{
			if(kv.second == value)
			{
				outKey = kv.first;
				return true;
			}
		}
		return false;
	}


};

#endif
