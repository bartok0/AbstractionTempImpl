#pragma once
#include <vector>

struct Triangle {
	std::vector<DirectX::XMFLOAT3> triangleVertices;
	std::vector<DirectX::XMFLOAT3> triangleNormals;
	DirectX::XMFLOAT3 position;

	Triangle(DirectX::XMFLOAT3 v1, DirectX::XMFLOAT3 v2, DirectX::XMFLOAT3 v3, DirectX::XMFLOAT3 n1, DirectX::XMFLOAT3 n2, DirectX::XMFLOAT3 n3) {
		triangleVertices.push_back(v1);
		triangleVertices.push_back(v2);
		triangleVertices.push_back(v3);
		position = DirectX::XMFLOAT3(
			(v1.x + v2.x + v3.x) / 3,
			(v1.y + v2.y + v3.y) / 3,
			(v1.z + v2.z + v3.z) / 3
		);
		triangleNormals.push_back(n1);
		triangleNormals.push_back(n2);
		triangleNormals.push_back(n3);
	}

	Triangle(std::vector<DirectX::XMFLOAT3> vertices) {
		float xSum, ySum, zSum = 0.0f;
		for (DirectX::XMFLOAT3 v : vertices) {
			xSum += v.x;
			ySum += v.y;
			zSum += v.z;
			triangleVertices.push_back(v);
		}
		position = DirectX::XMFLOAT3(
			xSum/3,
			ySum/3,
			zSum/3
		);
	}
};

