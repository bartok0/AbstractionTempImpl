#pragma once
#include "pch.h"
#include "Kdtree.h"

static bool sortX(Triangle A, Triangle B)
{
	return A.position.x < B.position.x;
}

static bool sortY(const Triangle A, const Triangle B)
{
	return A.position.y < B.position.y;
}

static bool sortZ(Triangle A, Triangle B)
{
	return A.position.z < B.position.z;
}

Kdtree::Node* Kdtree::Create(std::vector<Triangle> triangles, int depth, int maxdepth) {

	Node* node = new Node;
	unsigned int minTriangles = 2;
	if (triangles.size() >= minTriangles) {
		if (depth >= maxdepth) {
			node->type = LEAF;
			node->triangles = {};
			nodes.push_back(node);
			return node;
		} else {
			node->type = SPLIT;
			Axis splitAxis = Axis(depth % 3);
			node->split.first = splitAxis;
			int medianIndex = (int)(triangles.size() / 2);
			switch (splitAxis)
			{
			case X:
				std::sort(triangles.begin(), triangles.end(), sortX);
				node->split.second = triangles[medianIndex].position.x;
				break;
			case Y:
				std::sort(triangles.begin(), triangles.end(), sortY);
				node->split.second = triangles[medianIndex].position.y;
				break;
			case Z:
				std::sort(triangles.begin(), triangles.end(), sortZ);
				node->split.second = triangles[medianIndex].position.z;
				break;
			default:
				break;
			}

			std::vector<Triangle> lessTriangles = std::vector<Triangle>(triangles.begin(), triangles.begin() + medianIndex);
			std::vector<Triangle> moreTriangles = std::vector<Triangle>(triangles.begin() + medianIndex, triangles.end());
			node->leftChild = Create(lessTriangles, depth + 1, maxdepth);
			node->rightChild = Create(moreTriangles, depth + 1, maxdepth);
			nodes.push_back(node);
			return node;
		}
	} else {
		node->type = LEAF;
		node->triangles = {};
		nodes.push_back(node);
		return node;
	}
}

Kdtree::Node* Kdtree::SearchPos(DirectX::XMFLOAT3 pos, Node* rootNode)
{
	Node* node = rootNode;
	int currentDepth = 0;
	Axis currentAxis = X;
	float vertexPos;

	while (node->type != LEAF) {
		switch (currentAxis) {
		case X:
			vertexPos = pos.x;
			break;
		case Y:
			vertexPos = pos.y;
			break;
		case Z:
			vertexPos = pos.z;
			break;
		default:
			break;
		}
		if (vertexPos < node->split.second) {
			node = node->leftChild;
		} else {
			node = node->rightChild;
		}
		currentAxis = node->split.first;
	}
	return node;
}

std::vector<Triangle> Kdtree::SearchTri(Triangle triangle, Node * rootNode)
{
	std::vector<Triangle> triangles = {};
	std::vector<Node*> nodes;
	Node* node;

	for (DirectX::XMFLOAT3 vertex : triangle.triangleVertices) {
		node = SearchPos(vertex, rootNode);
		bool exists = false;
		for (std::vector<Node*>::iterator it = nodes.begin(); it != nodes.end(); it++) {
			if (*it == node)
				exists = true;
		}
		if (!exists) {
			nodes.push_back(node);
			triangles.insert(triangles.end(), node->triangles.begin(), node->triangles.end());
		}
	}
	return triangles;
}

void Kdtree::Insert(Triangle triangle, Node* rootNode)
{
	Node* currentNode;
	for (DirectX::XMFLOAT3 vertex : triangle.triangleVertices) {
		currentNode = SearchPos(vertex, rootNode);
		currentNode->triangles.push_back(triangle);
	}
	return;
}




