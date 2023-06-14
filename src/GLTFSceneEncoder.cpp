#include "GLTFSceneEncoder.h"

GLTFSceneEncoder::GLTFSceneEncoder()
{
}

GLTFSceneEncoder::~GLTFSceneEncoder()
{
}


void GLTFSceneEncoder::write(const std::string & filepath, const EncoderArguments & arguments)
{
	std::string ext = "";
	size_t pos = filepath.find_last_of(".");
	if (pos != std::string::npos)
	{
		ext = filepath.substr(pos + 1);
	}
	for (size_t i = 0; i < ext.size(); ++i)
		ext[i] = (char)tolower(ext[i]);



	std::string err;
	std::string war;
	bool loaded = false;

	tinygltf::TinyGLTF loader;
	
	if (ext.compare("gltf") == 0)
	{
		loaded = loader.LoadASCIIFromFile(&m_contexModel, &err, &war, filepath);
	}
	else if (ext.compare("glb") == 0)
	{
		loaded = loader.LoadBinaryFromFile(&m_contexModel, &err, &war, filepath);
	}
	

	if (!err.empty()) {
		LOG(1, "%s %s\n", "[LoaderGLTF] error: load gltf file failed:", err.c_str());
	}

	if (!war.empty()) {
		LOG(1, "%s %s\n", "[LoaderGLTF] warn: load gltf file failed:", war.c_str());
	}

	if (!loaded) {
		LOG(1, "[LoaderGLTF] Unable to load GLTF scene\n");
		return;
	}

	const tinygltf::Scene& defaultScene = m_contexModel.scenes[m_contexModel.defaultScene];

	// load scene
	Scene* gameScene = createScene(&defaultScene);
	m_gamePlayFile.addScene(gameScene);

	// collect resources
	for (auto kv : m_meshMapper)
	{
		m_gamePlayFile.addMesh(kv.second);
	}
	for (auto kv : m_nodeMapper)
	{
		m_gamePlayFile.addNode(kv.second);
	}
	
	for (auto kv : m_cameraMapper)
	{
		m_gamePlayFile.addCamera(kv.second);
	}
	for (auto kv : m_lightMapper)
	{
		m_gamePlayFile.addLight(kv.second);
	}

	// load animations
	loadAnimations(m_contexModel.animations);


	// load materials
	for (auto kv : m_meshMapper)
	{
		for (int i = 0; i < kv.first->primitives.size(); i++)
		{
			auto& submesh = kv.first->primitives[i];

			if (submesh.material < 0) { continue; }

			tinygltf::Material* gltfMat = &m_contexModel.materials[submesh.material];
			std::string gltfMatID = gltfMat->name;
			if (gltfMatID.empty())
			{
				gltfMatID = "Material_" + std::to_string(i);
			}

			bool hasVertexColor = kv.second->hasVertexColors();
			Material* gameMat = getOrCreateMaterial(gltfMat, gltfMatID,true,hasVertexColor);
	
			kv.second->model->setMaterial(gameMat, i);
		}

	}

	LOG(1, "[LoaderGLTF] Optimizing GamePlay Binary.\n");
	m_gamePlayFile.adjust();
	std::string outputFilePath = arguments.getOutputFilePath();
	if (arguments.textOutputEnabled())
	{
		int pos = outputFilePath.find_last_of('.');
		if (pos > 2)
		{
			std::string path = outputFilePath.substr(0, pos);
			path.append(".xml");
			LOG(1, "[LoaderGLTF] Saving debug file: %s\n", path.c_str());
			if (!m_gamePlayFile.saveText(path))
			{
				LOG(1, "Error writing text file: %s\n", path.c_str());
			}
		}
	}
	else
	{
		LOG(1, "[LoaderGLTF] Saving binary file: %s\n", outputFilePath.c_str());
		if (!m_gamePlayFile.saveBinary(outputFilePath))
		{
			LOG(1, "[LoaderGLTF] Error writing binary file: %s\n", outputFilePath.c_str());
		}

	}

	// Write the material file
	if (arguments.outputMaterialEnabled() || true)
	{
		int pos = outputFilePath.find_last_of('.');
		if (pos > 2)
		{
			std::string path = outputFilePath.substr(0, pos);
			path.append(".material");
			writeMaterial(path);
		}
	}

}

bool GLTFSceneEncoder::writeMaterial(const std::string & filepath)
{
	FILE* file = fopen(filepath.c_str(), "w");
	if (!file)
	{
		return false;
	}

	// Finds the base materials that are used.
	std::set<Material*> baseMaterialsToWrite;
	for (auto it: m_materialsMapper)
	{
		if(it.second->getParent() != nullptr)
		{
			baseMaterialsToWrite.insert(it.second->getParent());
		}
	}

	// Write the base materials that are used.
	for (auto it : baseMaterialsToWrite)
	{
		it->writeMaterial(file);
		fprintf(file, "\n");
	}

	// Write all of the non-base materials.
	for (auto it : m_materialsMapper)
	{
		it.second->writeMaterial(file);
		fprintf(file, "\n");
	}

	fclose(file);
	return true;
}

void GLTFSceneEncoder::loadAnimations(const std::vector<tinygltf::Animation>& gltfAnimas)
{
	for(int i = 0,n=gltfAnimas.size();i<n;i++)
	{
		std::string animaName = gltfAnimas[0].name;
		if(animaName.empty())
		{
			animaName = "Anima-" + std::to_string(i);
		}
		loadAnimation(gltfAnimas[i],animaName);
	}
}


void GLTFSceneEncoder::loadAnimation(const tinygltf::Animation & gltfAnima,const std::string& animaName)
{
	Animation* gameAnima = new Animation;
	gameAnima->setId(animaName);

	
	float startTime = 0, stopTime = 0;

	auto srtChannels = collectSRTChannel(&gltfAnima);

	for(auto kv:srtChannels)
	{
		tinygltf::Node* gltfTargetNode = kv.first;
		SRTChannel srtChannel = kv.second;

		Node* gameTargetNode = m_nodeMapper[gltfTargetNode];

		AnimationChannel* gameChannel = new AnimationChannel;
		gameAnima->add(gameChannel);

		gameChannel->setTargetId(gameTargetNode->getId());
		gameChannel->setInterpolation(AnimationChannel::LINEAR);

		srtChannel.sampleToGameAnimationChannel(gameChannel,this);

	}

	m_gamePlayFile.addAnimation(gameAnima);
}

std::map<tinygltf::Node*, GLTFSceneEncoder::SRTChannel> GLTFSceneEncoder::collectSRTChannel(
	const tinygltf::Animation* gltfAnima)
{
	// 收集 animation 中同一个node 对应的 scale rotation translate animation channel
	std::map<tinygltf::Node*, SRTChannel> result;
	float globalMin = FLT_MAX;
	float globalMax = -FLT_MAX;

	for(auto& channel:gltfAnima->channels)
	{
		tinygltf::Node* targetNode = &m_contexModel.nodes[channel.target_node];

		SRTChannel srtChannel;
		srtChannel.animation = gltfAnima;

		tryFindValue(result, targetNode, srtChannel);

		std::string targetPath = channel.target_path;
		if(targetPath == "scale")
		{
			srtChannel.scaleChannel = &channel;
		}
		else if(targetPath == "rotation")
		{
			srtChannel.rotationChannel = &channel;
		}
		else if(targetPath == "translation")
		{
			srtChannel.translationChannel = &channel;
		}
		else
		{
			//nop
		}
		
		auto timeAccessor = m_contexModel.accessors[gltfAnima->samplers[channel.sampler].input];
		globalMin = std::min(globalMin, float(timeAccessor.minValues[0]));
		globalMax = std::max(globalMax, float(timeAccessor.maxValues[0]));

		result[targetNode] = srtChannel;
	}

	for (auto& channel:result)
	{
		channel.second.startTime = globalMin;
		channel.second.stopTime = globalMax;
	}
	
	return result;
}


namespace 
{
	int getElementCount(int type)
	{
		switch (type) {
		case TINYGLTF_TYPE_SCALAR:
			return 1;
		case TINYGLTF_TYPE_VEC2:
			return 2;
		case TINYGLTF_TYPE_VEC3:
			return  3;
		case TINYGLTF_TYPE_VEC4:
			return 4;
		default:
			throw "not supported type";
		}
	}

	float readFloat(const tinygltf::Accessor* accessor, const tinygltf::BufferView* bufferView, const tinygltf::Buffer* buffer, int index)
	{
		int size = getElementCount(accessor->type);
		
		int stride = bufferView->byteStride > 0 ? bufferView->byteStride : size * sizeof(float);

		const void* pData = &(buffer->data.at(0)) + bufferView->byteOffset + accessor->byteOffset + index * stride;
		float result;
		std::memcpy(&result, pData, stride);
		return result;
	}

	Vector2 readVector2(const tinygltf::Accessor* accessor, const tinygltf::BufferView* bufferView, const tinygltf::Buffer* buffer, int index)
	{
		int size = getElementCount(accessor->type);

		int stride = bufferView->byteStride > 0 ? bufferView->byteStride : size * sizeof(float);

		const void* pData = &(buffer->data.at(0)) + bufferView->byteOffset + index * stride + accessor->byteOffset;
		float result[2];

		std::memcpy(&result[0], pData, stride);
		return Vector2(result[0], result[1]);
	}

	Vector3 readVector3(const tinygltf::Accessor* accessor, const tinygltf::BufferView* bufferView, const tinygltf::Buffer* buffer, int index)
	{
		int size = getElementCount(accessor->type);

		int stride = bufferView->byteStride > 0 ? bufferView->byteStride : size * sizeof(float);

		const void* pData = &(buffer->data.at(0)) + bufferView->byteOffset + index * stride + accessor->byteOffset;
		float result[3];
		
		std::memcpy(&result[0], pData, stride);
		return Vector3(result[0],result[1],result[2]);
	}

	Vector4 readVector4(const tinygltf::Accessor* accessor, const tinygltf::BufferView* bufferView, const tinygltf::Buffer* buffer, int index)
	{
		int size = getElementCount(accessor->type);

		int stride = bufferView->byteStride > 0 ? bufferView->byteStride : size * sizeof(float);

		const void* pData = &(buffer->data.at(0)) + bufferView->byteOffset + index * stride + accessor->byteOffset;
		float result[4];

		std::memcpy(&result[0], pData, stride);
		return Vector4(result[0], result[1], result[2],result[3]);
	}

}

void GLTFSceneEncoder::calculateLerpFactor(const tinygltf::Accessor* accessor, const tinygltf::BufferView* bufferView, const tinygltf::Buffer* buffer, float time, int & i0, int & i1, float & f)
{
	float minTime = accessor->minValues[0];
	float maxTime = accessor->maxValues[0];
	if(time <= minTime)
	{
		i0 = 0;
		i1 = 0;
		f = 0;
	}
	else if(time>=maxTime)
	{
		i0 = accessor->count - 1;
		i1 = accessor->count - 1;
		f = 0;
	}
	else
	{
		int left = 0;
		int right = accessor->count - 1;
		// 二分查找
		while (left < right) 
		{
			int mid = (left + right + 1) >> 1;
			float value = readFloat(accessor, bufferView, buffer, mid);
			if(value<=time)
			{
				left = mid;
			}
			else
			{
				right = mid - 1;
			}
		}
		i0 = left;
		i1 = std::min(static_cast<int>(left + 1),static_cast<int>(accessor->count - 1));
		float value0 = readFloat(accessor, bufferView, buffer, i0);
		float value1 = readFloat(accessor, bufferView, buffer, i1);
		f = (time - value0) / (value1 - value0);
	}
}

Vector3 GLTFSceneEncoder::sampleVector3(const tinygltf::AnimationSampler * sampler, float time)
{
	int timeAccessorId = sampler->input;
	tinygltf::Accessor& timeAccessor = m_contexModel.accessors[timeAccessorId];
	tinygltf::BufferView& timeBufferView = m_contexModel.bufferViews[timeAccessor.bufferView];
	tinygltf::Buffer& timeBuffer = m_contexModel.buffers[timeBufferView.buffer];
	int i0, i1;
	float fac;
	calculateLerpFactor(&timeAccessor,&timeBufferView,&timeBuffer, time, i0, i1, fac);

	tinygltf::Accessor& valueAccessor = m_contexModel.accessors[sampler->output];
	tinygltf::BufferView& valueBufferView = m_contexModel.bufferViews[valueAccessor.bufferView];
	tinygltf::Buffer& valueBuffer = m_contexModel.buffers[valueBufferView.buffer];

	Vector3 value0 = readVector3(&valueAccessor, &valueBufferView, &valueBuffer, i0);
	Vector3 value1 = readVector3(&valueAccessor, &valueBufferView, &valueBuffer, i1);

	return value0 * (1 - fac) + value1 * fac;

}

Quaternion GLTFSceneEncoder::sampleQuat(const tinygltf::AnimationSampler * sampler, float time)
{
	int timeAccessorId = sampler->input;
	tinygltf::Accessor& timeAccessor = m_contexModel.accessors[timeAccessorId];
	tinygltf::BufferView& timeBufferView = m_contexModel.bufferViews[timeAccessor.bufferView];
	tinygltf::Buffer& timeBuffer = m_contexModel.buffers[timeBufferView.buffer];
	int i0, i1;
	float fac;
	calculateLerpFactor(&timeAccessor, &timeBufferView, &timeBuffer, time, i0, i1, fac);

	tinygltf::Accessor& valueAccessor = m_contexModel.accessors[sampler->output];
	tinygltf::BufferView& valueBufferView = m_contexModel.bufferViews[valueAccessor.bufferView];
	tinygltf::Buffer& valueBuffer = m_contexModel.buffers[valueBufferView.buffer];

	Vector4 value0 = readVector4(&valueAccessor, &valueBufferView, &valueBuffer, i0);
	Vector4 value1 = readVector4(&valueAccessor, &valueBufferView, &valueBuffer, i1);

	Quaternion quat0 = Quaternion(value0.x, value0.y, value0.z, value0.w);
	Quaternion quat1 = Quaternion(value1.x, value1.y, value1.z, value1.w);
	Quaternion quatTarget;
	Quaternion::slerp(quat0, quat1, fac, &quatTarget);

	//Vector3 e;
	//float angle = quatTarget.toAxisAngle(&e);
	//printf("%f, %f, %f, %f, %f \n",e.x,e.y,e.z,angle/3.141592653*180,fac);

	return quatTarget;
}


Scene* GLTFSceneEncoder::createScene(const tinygltf::Scene * gltfScene)
{
	Scene* gameScene = new Scene;
	
	if (gltfScene->name.empty())
	{
		gameScene->setId("__SCENE__");
	}
	else
	{
		gameScene->setId(gltfScene->name);
	}

	for (auto itr :gltfScene->nodes)
	{
		const tinygltf::Node& gltfNode = m_contexModel.nodes[itr];
		std::string gltfNodeID = gltfNode.name;
		if(gltfNodeID.empty())
		{
			gltfNodeID = "Node_" + std::to_string(itr);
		}
		Node* gameNode = getOrCreateNode(&gltfNode, gltfNodeID);
		
		gameScene->add(gameNode);

	}

	gameScene->setActiveCameraNode(gameScene->getFirstCameraNode());
	return gameScene;
}

Node * GLTFSceneEncoder::getOrCreateNode(const tinygltf::Node* gltfNode, const std::string& gltfNodeID)
{
	static int id = 0;
	Node* gameNode = nullptr;
	if(tryFindValue(m_nodeMapper,const_cast<tinygltf::Node*>(gltfNode),gameNode))
	{
		return gameNode;
	}

	gameNode = new Node();
	gameNode->setId(gltfNodeID);
	m_nodeMapper[const_cast<tinygltf::Node*>(gltfNode)] = gameNode;

	// 构造变换信息
	setTransform(gltfNode, gameNode);

	// 构造 camera 组件
	setCameraComponent(gltfNode, gameNode);

	// 构造 light 组件
	setLightComponent(gltfNode, gameNode);

	// 构造 model 组件
	setModelComponent(gltfNode, gameNode);

	// 添加子 node
	for (auto itr : gltfNode->children)
	{
		const tinygltf::Node& gltfChildNode = m_contexModel.nodes[itr];

		std::string gltfChildNodeID = gltfChildNode.name;
		if (gltfChildNodeID.empty())
		{
			gltfChildNodeID = "Node_" + std::to_string(itr);
		}

		Node* gameChildNode = getOrCreateNode(&gltfChildNode, gltfChildNodeID);
		
		if (gameChildNode != nullptr)
		{
			gameNode->addChild(gameChildNode);
		}

	}

	return gameNode;
}

void GLTFSceneEncoder::setTransform(const tinygltf::Node * gltfNode, Node * gameNode)
{
	Matrix matrix;

	if (gltfNode->matrix.size() == 16)
	{
		for (int i = 0; i < 16; i++)
		{
			matrix.m[i] = (*gltfNode).matrix[i];
		}
	}
	else
	{
		if(gltfNode->scale.size() == 3)
		{
			matrix.scale(gltfNode->scale[0], gltfNode->scale[1], gltfNode->scale[2]);
		}
		if(gltfNode->rotation.size() == 4)
		{
			Quaternion quat(gltfNode->rotation[0], gltfNode->rotation[1], gltfNode->rotation[2], gltfNode->rotation[3]);
			matrix.rotate(quat);
		}
		if(gltfNode->translation.size() == 3)
		{
			matrix.translate(gltfNode->translation[0], gltfNode->translation[1], gltfNode->translation[2]);
		}
	}

	gameNode->setTransformMatrix(matrix.getArray());
}

bool GLTFSceneEncoder::setCameraComponent(const tinygltf::Node * gltfNode, Node * gameNode)
{
	return false;
}

bool GLTFSceneEncoder::setLightComponent(const tinygltf::Node * gltfNode, Node * gameNode)
{
	return false;
}

bool GLTFSceneEncoder::setModelComponent(const tinygltf::Node * gltfNode, Node * gameNode)
{
	if (gltfNode->mesh < 0) { return false; }

	const tinygltf::Mesh& gltfMesh = m_contexModel.meshes[gltfNode->mesh];

	std::string gltfMeshID = gltfMesh.name;
	if(gltfMeshID.empty())
	{
		gltfMeshID = "Mesh_" + std::to_string(gltfNode->mesh);
	}
	Mesh* gameMesh = getOrCreateMesh(&gltfMesh, gltfMeshID);
	
	Model* gameModel = new Model();
	gameModel->setMesh(gameMesh);
	gameNode->setModel(gameModel);
	
	setSkinComponent(&gltfMesh, gameModel);

	if (gameModel->getSkin() != nullptr)
	{
		gameNode->resetTransformMatrix();
	}
	return true;
}

Material* GLTFSceneEncoder::getOrCreateMaterial(const tinygltf::Material* gltfMat,const std::string& gltfID, bool isLit, bool hasVertexColor)
{
	Material* gameMat = nullptr;
	if(tryFindValue(m_materialsMapper,const_cast<tinygltf::Material*>(gltfMat),gameMat))
	{
		return gameMat;
	}
	gameMat = new Material(gltfID);
	if (hasVertexColor) {
		gameMat->addDefine(VERTEX_COLOR);
	}

	gameMat->setLit(isLit);

	m_materialsMapper[const_cast<tinygltf::Material*>(gltfMat)] = gameMat;

	// set uniform and texture
	setMaterialTextures(gltfMat, gameMat);
	setMaterialUniforms(gltfMat, gameMat);

	gameMat->setParent(findBaseMaterial(gameMat));
	return gameMat;
}

namespace 
{
	MeshPart::PrimitiveType translateGltfPrimitiveTypeToGamePrimitiveType(int gltfPrimitiveType)
	{
		// TINYGLTF_MODE_POINTS (0)
		// TINYGLTF_MODE_LINE (1)
		// TINYGLTF_MODE_LINE_LOOP (2)
		// TINYGLTF_MODE_LINE_STRIP (3)
		// TINYGLTF_MODE_TRIANGLES (4)
		// TINYGLTF_MODE_TRIANGLE_STRIP (5)
		// TINYGLTF_MODE_TRIANGLE_FAN (6)
		static const int map[7] = {
			TINYGLTF_MODE_POINTS,
			TINYGLTF_MODE_LINE,
			TINYGLTF_MODE_LINE_LOOP,
			TINYGLTF_MODE_LINE_STRIP,
			TINYGLTF_MODE_TRIANGLES,
			TINYGLTF_MODE_TRIANGLE_STRIP,
			TINYGLTF_MODE_TRIANGLE_FAN
		};
		return static_cast<MeshPart::PrimitiveType>(map[gltfPrimitiveType]);
	}
}
Mesh* GLTFSceneEncoder::getOrCreateMesh(const tinygltf::Mesh* gltfMesh, const std::string& gltfNodeID)
{
	Mesh* gameMesh = nullptr;
	if(tryFindValue(m_meshMapper,const_cast<tinygltf::Mesh*>(gltfMesh),gameMesh))
	{
		return gameMesh;
	}
	
	gameMesh = new Mesh;
	gameMesh->setId(gltfNodeID);

	m_meshMapper[const_cast<tinygltf::Mesh*>(gltfMesh)] = gameMesh;

	std::vector<int> lengths;
	std::vector<Vertex> vertexData = loadVertexData(gltfMesh,lengths);

	if (vertexData.empty()) { return gameMesh; }

	auto vertex0 = vertexData[0];
	gameMesh->addVetexAttribute(POSITION, Vertex::POSITION_COUNT);
	if(vertex0.hasNormal)
	{
		gameMesh->addVetexAttribute(NORMAL, Vertex::NORMAL_COUNT);
	}
	if(vertex0.hasTangent)
	{
		gameMesh->addVetexAttribute(TANGENT, Vertex::TANGENT_COUNT);
	}
	if(vertex0.hasBinormal)
	{
		gameMesh->addVetexAttribute(BINORMAL, Vertex::BINORMAL_COUNT);
	}
	for(int i =0;i<8;i++)
	{
		if(vertex0.hasTexCoord[i])
		{
			gameMesh->addVetexAttribute(TEXCOORD0 + i, Vertex::TEXCOORD_COUNT);
		}
	}
	for (auto& v : vertexData)
	{
		gameMesh->addVertex(v);
	}

	int idOffset = 0;
	for (int i = 0;i<gltfMesh->primitives.size();i++)
	{
		if(i == 0)
		{
			idOffset += 0;
		}
		else 
		{
			idOffset += lengths[i - 1];
		}
		const auto& subMesh = gltfMesh->primitives[i];
		MeshPart* gameSubMesh = new MeshPart;
		gameSubMesh->setPrimitiveType(translateGltfPrimitiveTypeToGamePrimitiveType(subMesh.mode));

		std::vector<int> indexData;
		loadIndexData(&subMesh, indexData);
		for (auto& i : indexData)
		{
			gameSubMesh->addIndex(i + idOffset);
		}
		gameMesh->addMeshPart(gameSubMesh);
	}

	return gameMesh;
}

std::vector<Vertex> GLTFSceneEncoder::loadVertexData(const tinygltf::Mesh* gltfMesh,std::vector<int>& lengths)
{
	// load the scene first.
	float cache[4];

	std::vector<std::vector<Vertex>> submeshesData;
	
	if (gltfMesh->primitives.empty()) {
		return std::vector<Vertex>();
	}


	for (size_t i = 0; i < gltfMesh->primitives.size(); ++i) {
		const tinygltf::Primitive& primitive = gltfMesh->primitives[i];
		if (primitive.indices < 0) {
			continue;
		}

		size_t num_vert = 0;
		std::vector<Vertex> meshData;
		bool firstTimeParse = true;
		for (auto it : primitive.attributes) 
		{
			if (it.second < 0) continue;

			const tinygltf::Accessor& accessor = m_contexModel.accessors[it.second];
			const tinygltf::BufferView& bufferView = m_contexModel.bufferViews[accessor.bufferView];
			const tinygltf::Buffer& buffer = m_contexModel.buffers[bufferView.buffer];

			if (firstTimeParse)
			{
				num_vert = accessor.count;
				meshData.resize(num_vert);
				firstTimeParse = false;
			}

			int size = getElementCount(accessor.type);

			int stride = bufferView.byteStride > 0 ? bufferView.byteStride : size * sizeof(float);

			if (!it.first.compare("POSITION"))
			{
				for (auto k = 0; k < num_vert; ++k)
				{
					const void* pData = &(buffer.data.at(0)) + bufferView.byteOffset + accessor.byteOffset + k * stride;
					std::memcpy(cache, pData, stride);
					meshData[k].position.set(cache);
				}
			}
			else if (!it.first.compare("NORMAL")) {
				for (auto k = 0; k < num_vert; ++k) {
					const void* pData = &(buffer.data.at(0)) + bufferView.byteOffset + accessor.byteOffset + k * stride;
					meshData[k].hasNormal = true;
					std::memcpy(cache, pData, stride);
					meshData[k].normal.set(cache);
				}
			}
			else if (!it.first.compare("TANGENT")) {
				for (auto k = 0; k < num_vert; ++k) {
					const void* pData = &(buffer.data.at(0)) + bufferView.byteOffset + accessor.byteOffset + k * stride;
					meshData[k].hasTangent = true;
					std::memcpy(cache, pData, stride);
					meshData[k].tangent.set(cache);
					std::memcpy(&meshData[k].tangent, pData, stride);
				}
			}
			else if (!it.first.compare("BINORMAL")) {
				for (auto k = 0; k < num_vert; ++k) {
					const void* pData = &(buffer.data.at(0)) + bufferView.byteOffset + accessor.byteOffset + k * stride;
					meshData[k].hasBinormal = true;
					std::memcpy(cache, pData, stride);
					meshData[k].binormal.set(cache);
				}
			}
			else if (!it.first.compare("TEXCOORD_0")) {
				for (auto k = 0; k < num_vert; ++k) {
					const void* pData = &(buffer.data.at(0)) + bufferView.byteOffset + accessor.byteOffset + k * stride;
					meshData[k].hasTexCoord[0] = true;
					std::memcpy(cache, pData, stride);
					cache[1] = 1 - cache[1];
					meshData[k].texCoord[0].set(cache);
				}
			}
			else if (!it.first.compare("TEXCOORD_1")) {
				for (auto k = 0; k < num_vert; ++k) {
					const void* pData = &(buffer.data.at(0)) + bufferView.byteOffset + accessor.byteOffset + k * stride;
					meshData[k].hasTexCoord[1] = true;
					std::memcpy(cache, pData, stride);
					meshData[k].texCoord[1].set(cache);
				}
			}
			else if (!it.first.compare("TEXCOORD_2")) {
				for (auto k = 0; k < num_vert; ++k) {
					const void* pData = &(buffer.data.at(0)) + bufferView.byteOffset + accessor.byteOffset + k * stride;
					meshData[k].hasTexCoord[2] = true;
					meshData[k].texCoord[2].set(cache);
				}
			}
			else {
				// data type is not support
			}
		}

		submeshesData.emplace_back(meshData);
	}

	std::vector<Vertex> result;
	for(const auto& itr:submeshesData)
	{
		for(const auto& v:itr)
		{
			result.push_back(v);
		}
		lengths.push_back(itr.size());
	}
	return result;
}

void GLTFSceneEncoder::loadIndexData(const tinygltf::Primitive* gltfSubmesh, std::vector<int>& indexData)
{
	// Try to get the data from the index buffer.
	const tinygltf::Accessor& indexAccessor = m_contexModel.accessors[gltfSubmesh->indices];
	size_t numIndices = indexAccessor.count;
	indexData.resize(numIndices);
	const tinygltf::BufferView& indexBufferView = m_contexModel.bufferViews[indexAccessor.bufferView];
	const tinygltf::Buffer& indexBuffer = m_contexModel.buffers[m_contexModel.bufferViews[indexAccessor.bufferView].buffer];
	int size;
	switch (indexAccessor.componentType) {
	case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
		size = sizeof(uint8_t);
		break;
	case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
		size = sizeof(unsigned short);
		break;
	case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
		size = sizeof(unsigned int);
		break;
	default:
		return ;
	}

	for (auto k = 0; k < numIndices; ++k) {
		const void* pData = &(indexBuffer.data.at(0)) + indexBufferView.byteOffset + indexAccessor.byteOffset + k * size;
		std::memcpy(&(indexData[k]), pData, size);
	}
}

bool GLTFSceneEncoder::setSkinComponent(const tinygltf::Mesh * gltfMesh, Model * gameModel)
{
	return false;
}

void GLTFSceneEncoder::setMaterialTextures(const tinygltf::Material* gltfMat, Material* gameMat)
{
	int baseColorTexID = gltfMat->pbrMetallicRoughness.baseColorTexture.index;
	int roughnessID = gltfMat->pbrMetallicRoughness.metallicRoughnessTexture.index;
	int normalID = gltfMat->normalTexture.index;
	int emissiveID = gltfMat->emissiveTexture.index;
	int occlusionID = gltfMat->occlusionTexture.index;

	// use deffuseTexture
	if(baseColorTexID>=0)
	{
		Sampler* sampler = gameMat->createSampler("u_diffuseTexture");
		auto& gltfTexture  = m_contexModel.textures[baseColorTexID];
		auto& glftImage = m_contexModel.images[gltfTexture.source];
		
		std::string fileName = glftImage.uri;

		sampler->set("relativePath", fileName);
		sampler->set("wrapS", REPEAT);
		sampler->set("wrapT", REPEAT);

	}
}

namespace 
{
	std::string toString(double r,double g,double b,double a)
	{
		std::ostringstream ss;
		ss << r << ", " << g << ", " << b<<", "<<a;
		return ss.str();
	}
}
void GLTFSceneEncoder::setMaterialUniforms(const tinygltf::Material* gltfMat, Material* gameMat)
{
	std::vector<double> diffuseColor = gltfMat->pbrMetallicRoughness.baseColorFactor;
	gameMat->setUniform("u_diffuseColor", toString(diffuseColor[0], diffuseColor[1], diffuseColor[2], diffuseColor[3]));
}

namespace 
{

	std::string getBaseMaterialName(Material* material)
	{
		std::ostringstream baseName;
		if (material->isTextured())
		{
			baseName << "textured";
		}
		else
		{
			baseName << "colored";
		}

		return baseName.str();
	}
}
Material* GLTFSceneEncoder::findBaseMaterial(Material* gameMat)
{
	std::string baseMaterialName = getBaseMaterialName(gameMat);
	Material* baseMaterial = nullptr;

	auto baseMatItr = m_baseMaterialMapper.find(baseMaterialName);

	if(tryFindValue(m_baseMaterialMapper,baseMaterialName,baseMaterial))
	{
		return baseMaterial;
	}
	else
	{
		baseMaterial = createBaseMaterial(baseMaterialName, gameMat);
		m_baseMaterialMapper[baseMaterialName] = baseMaterial;
		return baseMaterial;
	}

	
}

Material* GLTFSceneEncoder::createBaseMaterial(const std::string& baseMaterialName, Material* childMaterial)
{
	Material* baseMaterial = new Material(baseMaterialName);
	baseMaterial->setUniform("u_worldViewProjectionMatrix", "WORLD_VIEW_PROJECTION_MATRIX");
	baseMaterial->setRenderState("cullFace", "true");
	baseMaterial->setRenderState("depthTest", "true");

	if (childMaterial->isTextured())
	{
		baseMaterial->setVertexShader("res/shaders/textured.vert");
		baseMaterial->setFragmentShader("res/shaders/textured.frag");

		Sampler* sampler = baseMaterial->createSampler(u_diffuseTexture);
		sampler->set("mipmap", "true");
		sampler->set("wrapS", CLAMP);
		sampler->set("wrapT", CLAMP);
		sampler->set(MIN_FILTER, LINEAR_MIPMAP_LINEAR);
		sampler->set(MAG_FILTER, LINEAR);
	}
	else
	{
		baseMaterial->setVertexShader("res/shaders/colored.vert");
		baseMaterial->setFragmentShader("res/shaders/colored.frag");
	}

	if (childMaterial->isLit())
	{
		baseMaterial->setLit(true);
		childMaterial->setUniform("u_inverseTransposeWorldViewMatrix", "INVERSE_TRANSPOSE_WORLD_VIEW_MATRIX");
		baseMaterial->addDefine("DIRECTIONAL_LIGHT_COUNT 1");
		if (childMaterial->isSpecular())
		{
			childMaterial->setUniform("u_cameraPosition", "CAMERA_WORLD_POSITION");
		}
	}
	assert(baseMaterial);
	return baseMaterial;
}

unsigned GLTFSceneEncoder::SRTChannel::getTargetAttribute()
{
	bool s = scaleChannel != nullptr;
	bool r = rotationChannel != nullptr;
	bool t = translationChannel != nullptr;

	if (s)
	{
		if (r)
		{
			if (t)
			{
				return Transform::ANIMATE_SCALE_ROTATE_TRANSLATE;
			}
			else
			{
				return Transform::ANIMATE_SCALE_ROTATE;
			}
		}
		else
		{
			if (t)
			{
				return Transform::ANIMATE_SCALE_TRANSLATE;
			}
			else
			{
				return Transform::ANIMATE_SCALE;
			}
		}
	}
	else
	{
		if (r)
		{
			if (t)
			{
				return Transform::ANIMATE_ROTATE_TRANSLATE;
			}
			else
			{
				return Transform::ANIMATE_ROTATE;
			}
		}
		else
		{
			if (t)
			{
				return Transform::ANIMATE_TRANSLATE;
			}
			else
			{
				return 0;
			}
		}
	}

	return 0;
}

void GLTFSceneEncoder::SRTChannel::sampleToGameAnimationChannel(AnimationChannel* gameAnimaChannel, GLTFSceneEncoder* encoder)
{
	unsigned targetAttribute = getTargetAttribute();
	gameAnimaChannel->setTargetAttribute(targetAttribute);

	std::vector<float>& times = gameAnimaChannel->getKeyTimes();
	times.clear();

	std::vector<float>& values = gameAnimaChannel->getKeyValues();
	values.clear();

	float deltaTime = 1.0f/ 30;
	float curTime = startTime;
	while(curTime<stopTime)
	{
		times.push_back(curTime*1000); //ms


		if(scaleChannel != nullptr)
		{
			Vector3 v = encoder->sampleVector3(&animation->samplers[scaleChannel->sampler],curTime);
			values.push_back(v.x);
			values.push_back(v.y);
			values.push_back(v.z);
		}

		if(rotationChannel != nullptr)
		{
			Quaternion v = encoder->sampleQuat(&animation->samplers[rotationChannel->sampler], curTime);
			values.push_back(v.x);
			values.push_back(v.y);
			values.push_back(v.z);
			values.push_back(v.w);

			Vector3 a;

		}

		if(translationChannel != nullptr)
		{
			Vector3 v = encoder->sampleVector3(&animation->samplers[translationChannel->sampler], curTime);
			values.push_back(v.x);
			values.push_back(v.y);
			values.push_back(v.z);
		}

		curTime += deltaTime;
	}

}
