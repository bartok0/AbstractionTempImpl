#pragma once

#include "Triangle.h"
#include <algorithm>

using namespace DirectX;

class Kdtree
{
public:
	Kdtree() {}
	~Kdtree() {
		for (Node* node : nodes) {
			delete node;
		}
	}

	enum Axis {
		X = 0, Y = 1, Z = 2
	};

	enum NodeType{
		SPLIT, LEAF
	};

	struct Node
	{
		NodeType type;
		std::pair<Axis, float> split;
		Node* leftChild = nullptr;
		Node* rightChild = nullptr;
		std::vector<Triangle> triangles;
	};

	Node* Create(std::vector<Triangle> triangles, int depth, int maxdepth);
	Node* SearchPos(DirectX::XMFLOAT3 pos, Node* rootNode);
	std::vector<Triangle> SearchTri(Triangle triangle, Node* rootNode);
	void Insert(Triangle triangle, Node* rootNode);
private:
	std::vector<Node*> nodes;
};

