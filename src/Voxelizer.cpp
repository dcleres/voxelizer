/*
 * Voxelizer.cpp
 *
 *  Created on: 22 Jun, 2014
 *      Author: chenqian
 */

#include "Voxelizer.h"
#include <exception>



Voxelizer::Voxelizer(int size, const string& pFile): _size(size) {
	cout << "voxelizer init... " << endl;
	_isInit = false;
	const aiScene* scene;
	try {
		/*
		 * Load scene
		 * */
		Assimp::Importer importer;
		scene = importer.ReadFile(pFile, aiProcessPreset_TargetRealtime_Fast);
		if (!scene) {
			throw std::runtime_error("Scene fails to be loaded!");
		}
		aiMesh* mesh = scene->mMeshes[0];
		_size2 = _size*_size;
		_totalSize = size*size*size/BATCH_SIZE;

		/**
		 * Reset voxels.
		 */
		_voxels.reset(new auint[_totalSize], ArrayDeleter<auint>());
		_voxelsBuffer.reset(new auint[_totalSize], ArrayDeleter<auint>());
		memset(_voxels.get(), 0, _totalSize * sizeof(int));
 		memset(_voxelsBuffer.get(), 0, _totalSize * sizeof(int));

		/**
		 * Store info.
		 */
		_numVertices = mesh->mNumVertices;
		_numFaces = mesh->mNumFaces;
		std::cout << "faces : " << _numFaces << std::endl;
		std::cout << "vertices : " << _numVertices << std::endl;
		_loadFromMesh(mesh);

		if (!scene) delete scene;
		_isInit = true;
	} catch (std::exception& e) {
		std::cout << e.what() << std::endl;
		if (!scene) delete scene;
	}
	cout << "done." << endl;
}


/**
 * Given voxel (int x, int y, int z), return loc (float x, float y, float z)
 */
inline v3_p Voxelizer::_getLoc(const v3_p& voxel) {
	Vec3f tmp = *_lb + (*_bound) * (*voxel) / (float) _size;
	v3_p loc(new Vec3f(tmp));
	return loc;
}


inline v3_p Voxelizer::_getLoc(const Vec3f& voxel) {
	Vec3f tmp = *_lb + (*_bound) * (voxel) / (float) _size;
	v3_p loc(new Vec3f(tmp));
	return loc;
}

/**
 * Given loc (float x, float y, float z), return voxel (int x, int y, int z)
 */
inline v3_p Voxelizer::_getVoxel(const Vec3f& loc) {
	Vec3f tmp = (loc - (*_lb)) * (float) _size / (*_bound);
	v3_p voxel(new Vec3f((int)tmp[0], (int)tmp[1], (int)tmp[2]));
	return voxel;
}

/**
 * Given loc (float x, float y, float z), return voxel (int x, int y, int z)
 */
inline v3_p Voxelizer::_getVoxel(const v3_p& loc) {
	Vec3f tmp = ((*loc) - (*_lb)) * (float) _size / (*_bound);
	v3_p voxel(new Vec3f((int)tmp[0], (int)tmp[1], (int)tmp[2]));
	return voxel;
}



/**
 *Get the collision object form triangle(face) id;
 */
inline tri_p Voxelizer::_getTri(const int triId) {
	const Vec3f& vIds = _faces.get()[triId];
	tri_p tri(new TriangleP(_vertices.get()[(int)vIds[0]], _vertices.get()[(int)vIds[1]], _vertices.get()[(int)vIds[2]]));
	return tri;
}

/**
 * Load info from mesh.
 */
inline void Voxelizer::_loadFromMesh(const aiMesh* mesh) {
	_vertices.reset(new Vec3f[_numVertices], ArrayDeleter<Vec3f>());
	Vec3f tmp;
	for (size_t i = 0; i < _numVertices; ++i) {
		_vertices.get()[i] = Vec3f(mesh->mVertices[i].x, mesh->mVertices[i].y,
				mesh->mVertices[i].z);
		if (i == 0) {
			_meshLb.reset(new Vec3f(mesh->mVertices[i].x, mesh->mVertices[i].y,
							mesh->mVertices[i].z));
			_meshUb.reset(new Vec3f(mesh->mVertices[i].x, mesh->mVertices[i].y,
							mesh->mVertices[i].z));
		} else {
			_meshLb->ubound(_vertices.get()[i]); // bug1
			_meshUb->lbound(_vertices.get()[i]); // bug2
		}
	}

	_meshLb.reset(new Vec3f((*_meshLb)-Vec3f(0.0001, 0.0001, 0.0001)));
	_meshUb.reset(new Vec3f((*_meshUb)+Vec3f(0.0001, 0.0001, 0.0001)));


	/**
	*
	*/
	_minLb = (*_meshLb)[0];
	_minLb = min(_minLb, (float)(*_meshLb)[1]);
	_minLb = min(_minLb, (float)(*_meshLb)[2]);
	_maxUb = (*_meshUb)[0];
	_maxUb = max(_maxUb, (float)(*_meshUb)[1]);
	_maxUb = max(_maxUb, (float)(*_meshUb)[2]);
	_lb.reset(new Vec3f(_minLb, _minLb, _minLb));
	_ub.reset(new Vec3f(_maxUb, _maxUb, _maxUb));
	_bound.reset(new Vec3f((*_ub - *_lb)));

	_faces.reset(new Vec3f[_numFaces], ArrayDeleter<Vec3f>());
	for (size_t i = 0; i < _numFaces; ++i) {
		_faces.get()[i] = Vec3f(mesh->mFaces[i].mIndices[0],
				mesh->mFaces[i].mIndices[1], mesh->mFaces[i].mIndices[2]);
	}
	_randomPermutation(_faces, _numFaces);

	Vec3f halfUnit = (*_bound) / ((float) _size*2);
	_halfUnit.reset(new Vec3f(halfUnit));
	_meshVoxLB = _getVoxel(_meshLb);
	_meshVoxUB = _getVoxel(_meshUb);

	cout << "space : " << *_lb << ", " << *_ub << endl;
	cout << "mesh bound : " << *_meshLb << ", " << *_meshUb << endl;
}


/**
 * voxelize the surface.
 */
void Voxelizer::voxelizeSurface(const int numThread) {
	if (!_isInit) {
		return;
	}
	cout << "surface voxelizing... " << endl;
	ThreadPool tp(numThread);
	for (int i = 0; i < _numFaces; ++i) {
	 tp.run(boost::bind(&Voxelizer::_runSurfaceTask, this, i));
	}
	tp.stop();
	cout << "done." << endl;
}

/**
 * Details of surface task.
 */
inline void Voxelizer::_runSurfaceTask(const int triId) {
	tri_p tri = _getTri(triId);
	tri->computeLocalAABB();
	const v3_p lb = _getVoxel(tri->aabb_local.min_);
	const v3_p ub = _getVoxel(tri->aabb_local.max_);
	int lx = (*lb)[0], ux = (*ub)[0], ly = (*lb)[1], uy = (*ub)[1], lz = (*lb)[2], uz = (*ub)[2];

	/**
	 * when the estimated voxels are too large, optimize with bfs.
	 */
	int count = 0;
	int esti = min(ux - lx, min(uy - ly, uz - lz));
	unsigned int voxelInt, tmp;
	if (esti < 100) {
		v3_p vxlBox(new Vec3f(0, 0, 0));
		for (int x = lx, y, z; x <= ux; ++x) {
			for (y = ly; y <= uy; ++y) {
				for (z = lz; z <= uz; ++z) {
					voxelInt = x*_size2 + y*_size + z;
					tmp = (_voxels.get())[voxelInt / BATCH_SIZE].load();
					if (GETBIT(tmp, voxelInt)) continue;
					vxlBox->setValue(x, y, z);
					if (collide(_halfUnit, _getLoc(vxlBox), tri)) {
						(_voxels.get())[voxelInt / BATCH_SIZE] |= (1<< (voxelInt % BATCH_SIZE));
						count++;
					}
				}
			}
		}
	} else {
		count = _bfsSurface(tri, lb, ub);
	}

}

inline int Voxelizer::_bfsSurface(const tri_p& tri, const v3_p& lb, const v3_p& ub) {
	queue<unsigned int> q;
	hash_set set;
	unsigned int start = _convVoxelToInt(_getVoxel(tri->a)), topVoxelInt, tmp, newVoxelInt;
	q.push(start);
	set.insert(start);
	v3_p topVoxel;
	int count = 0;
	while (!q.empty()) {
		count++;
		topVoxelInt = q.front();
		q.pop();
		tmp = (_voxels.get())[topVoxelInt/BATCH_SIZE].load();
		topVoxel = _convIntToVoxel(topVoxelInt);
		if (GETBIT(tmp, topVoxelInt) || collide(_halfUnit, _getLoc(topVoxel), tri)) {
			if (!GETBIT(tmp, topVoxelInt)) {
				(_voxels.get())[topVoxelInt / BATCH_SIZE] |= (1<<(topVoxelInt % BATCH_SIZE));
			}
			for (int i = 0; i < 6; ++i) {
				Vec3f newVoxel = *topVoxel + D_6[i];
				if (!_inRange(newVoxel, lb, ub)) continue;
				newVoxelInt = _convVoxelToInt(newVoxel);
				if (set.find(newVoxelInt) == set.end()) {
					set.insert(newVoxelInt);
					q.push(newVoxelInt);
				}
			}
		}
	}
	return count;
}

void Voxelizer::voxelizeSolid(int numThread) {
	if (!_isInit) {
		return;
	}
	cout << "solid voxelizing... " << endl;
	cout << "round 1..." << endl;
	_runSolidTask(numThread);
	cout << "round 2..." << endl;
	_runSolidTask2(numThread);
	for (int i = 0; i < _totalSize; ++i) _voxels.get()[i] = _voxelsBuffer.get()[i]^(~0);
	cout << "done." << endl;
}

inline void Voxelizer::_bfsSolid(const unsigned int startInt) {

	unsigned int voxelInt = startInt, tmp = (_voxels.get())[voxelInt/BATCH_SIZE].load() | (_voxelsBuffer.get())[voxelInt/BATCH_SIZE].load();
	if (GETBIT(tmp, voxelInt)) return;
	queue<unsigned int> q;
	q.push(voxelInt);
	v3_p topVoxel(new Vec3f(0, 0, 0));
	while (!q.empty()) {
		voxelInt = q.front();
		tmp = (_voxels.get())[voxelInt/BATCH_SIZE].load() | (_voxelsBuffer.get())[voxelInt/BATCH_SIZE].load();
		q.pop();
		topVoxel = _convIntToVoxel(voxelInt);
		if (!GETBIT(tmp, voxelInt)) {
			(_voxelsBuffer.get())[voxelInt / BATCH_SIZE] |= (1<<(voxelInt % BATCH_SIZE));
			for (int i = 0; i < 6; i++) {
				Vec3f newVoxel = *topVoxel + D_6[i];
				if (!_inRange(newVoxel, _meshVoxLB, _meshVoxUB)) continue;
				voxelInt = _convVoxelToInt(newVoxel);
				tmp = (_voxels.get())[voxelInt/BATCH_SIZE].load() | (_voxelsBuffer.get())[voxelInt/BATCH_SIZE].load();
				if(!GETBIT(tmp, voxelInt)) q.push(voxelInt);
			}
		}

	}
}

inline bool Voxelizer::_inRange(const Vec3f& vc, const v3_p& lb, const v3_p& ub) {
	return vc[0]>=(*lb)[0] && vc[0]<=(*ub)[0] && vc[1]>=(*lb)[1] && vc[1]<=(*ub)[1] && vc[2]>=(*lb)[2] && vc[2]<=(*ub)[2];
}

inline bool Voxelizer::_inRange(const int& x, const int& y, const int& z, const int& lx, const int& ly, const int& lz, const int& ux, const int& uy, const int& uz) {
	return x>=lx && x<=ux && y>=ly && y<=uy && z>=lz && z<=uz;
}


inline v3_p Voxelizer::_convIntToVoxel(const unsigned int& coord) {
	v3_p voxel(new Vec3f(coord/_size2, (coord/_size)%_size, coord%_size));
	return voxel;
}

inline unsigned int Voxelizer::_convVoxelToInt(const v3_p& voxel) {
	return (*voxel)[0]*_size2 + (*voxel)[1]*_size + (*voxel)[2];
}

inline unsigned int Voxelizer::_convVoxelToInt(const Vec3f& voxel) {
	return voxel[0]*_size2 + voxel[1]*_size + voxel[2];
}


inline void Voxelizer::_randomPermutation(const v3_p& data, int num) {
	for (int i = 0, id; i < num; ++i) {
		id = random(i, num-1);
		if (i != id) swap((data.get())[i], (data.get())[id]);
	}
}

inline void Voxelizer::_fillYZ(const int x) {
	int ly = (*_meshVoxLB)[1], uy = (*_meshVoxUB)[1], lz = (*_meshVoxLB)[2], uz = (*_meshVoxUB)[2];
	unsigned int voxelInt, tmp;
	for (int y = ly, z; y <= uy; ++y) {
		for (z = lz; z <= uz; ++z) {
			voxelInt = x*_size2 + y*_size + z;
			tmp = (_voxels.get())[voxelInt/BATCH_SIZE].load();
			if (GETBIT(tmp, voxelInt)) break;
			else {
				(_voxelsBuffer.get())[voxelInt / BATCH_SIZE] |= (1<< (voxelInt % BATCH_SIZE));
			}
		}
		if (z == uz+1) continue;
		for (z = uz; z >= lz; --z) {
			voxelInt = x*_size2 + y*_size + z;
			tmp = (_voxels.get())[voxelInt/BATCH_SIZE].load();
			if (GETBIT(tmp, voxelInt)) break;
			else {
				(_voxelsBuffer.get())[voxelInt / BATCH_SIZE] |= (1<< (voxelInt % BATCH_SIZE));
			}
		}
	}
}

inline void Voxelizer::_fillYZ2(const int x) {
	int lx = (*_meshVoxLB)[0], ux = (*_meshVoxUB)[0], ly = (*_meshVoxLB)[1], uy = (*_meshVoxUB)[1], lz = (*_meshVoxLB)[2], uz = (*_meshVoxUB)[2], nx, ny;
	unsigned int voxelInt, tmp;
	for (int y = ly, z; y <= uy; ++y) {
		for (z = lz; z <= uz; ++z) {
			voxelInt = x*_size2 + y*_size + z;
			tmp = (_voxels.get())[voxelInt/BATCH_SIZE].load();
			if (GETBIT(tmp, voxelInt)) break;
			else {
				for (int i = 0; i < 4; ++i) {
					nx = x+DI_4[i][0]; ny = y+DI_4[i][1];
					if (nx>=lx && nx<=ux && ny>=ly && ny<=uy) {
						voxelInt = nx*_size2 + ny*_size + z;
						tmp = (_voxels.get())[voxelInt/BATCH_SIZE].load() | (_voxelsBuffer.get())[voxelInt/BATCH_SIZE].load();
						if (!GETBIT(tmp, voxelInt)) _bfsSolid(voxelInt);
					}
				}
			}
		}
		if (z == uz+1) continue;
		for (z = uz; z >= lz; --z) {
			voxelInt = x*_size2 + y*_size + z;
			tmp = (_voxels.get())[voxelInt/BATCH_SIZE].load();
			if (GETBIT(tmp, voxelInt)) break;
			else {
				for (int i = 0; i < 4; ++i) {
					nx = x+DI_4[i][0]; ny = y+DI_4[i][1];
					if (nx>=lx && nx<=ux && ny>=ly && ny<=uy) {
						voxelInt = nx*_size2 + ny*_size + z;
						tmp = (_voxels.get())[voxelInt/BATCH_SIZE].load() | (_voxelsBuffer.get())[voxelInt/BATCH_SIZE].load();
						if (!GETBIT(tmp, voxelInt)) _bfsSolid(voxelInt);
					}
				}
			}
		}
	}
}

inline void Voxelizer::_fillXZ(const int y) {
	int lx = (*_meshVoxLB)[0], ux = (*_meshVoxUB)[0], lz = (*_meshVoxLB)[2], uz = (*_meshVoxUB)[2];
	unsigned int voxelInt, tmp;
	for (int z = lz, x; z <= uz; ++z) {
		for (x = lx; x <= ux; ++x) {
			voxelInt = x*_size2 + y*_size + z;
			tmp = (_voxels.get())[voxelInt/BATCH_SIZE].load();
			if (GETBIT(tmp, voxelInt)) break;
			else {
				(_voxelsBuffer.get())[voxelInt / BATCH_SIZE] |= (1<< (voxelInt % BATCH_SIZE));
			}
		}
		if (x == ux+1) continue;
		for (x = ux; x >= lx; --x) {
			voxelInt = x*_size2 + y*_size + z;
			tmp = (_voxels.get())[voxelInt/BATCH_SIZE].load();
			if (GETBIT(tmp, voxelInt)) break;
			else {
				(_voxelsBuffer.get())[voxelInt / BATCH_SIZE] |= (1<< (voxelInt % BATCH_SIZE));
			}
		}
	}
}

inline void Voxelizer::_fillXZ2(const int y) {
	int lx = (*_meshVoxLB)[0], ux = (*_meshVoxUB)[0], ly = (*_meshVoxLB)[1], uy = (*_meshVoxUB)[1], lz = (*_meshVoxLB)[2], uz = (*_meshVoxUB)[2], ny, nz;
	unsigned int voxelInt, tmp;
	for (int z = lz, x; z <= uz; ++z) {
		for (x = lx; x <= ux; ++x) {
			voxelInt = x*_size2 + y*_size + z;
			tmp = (_voxels.get())[voxelInt/BATCH_SIZE].load();
			if (GETBIT(tmp, voxelInt)) break;
			else {
				for (int i = 0; i < 4; ++i) {
					ny = y+DI_4[i][0]; nz = z+DI_4[i][1];
					if (nz>=lz && nz<=uz && ny>=ly && ny<=uy) {
						voxelInt = x*_size2 + ny*_size + nz;
						tmp = (_voxels.get())[voxelInt/BATCH_SIZE].load() | (_voxelsBuffer.get())[voxelInt/BATCH_SIZE].load();
						if (!GETBIT(tmp, voxelInt)) _bfsSolid(voxelInt);
					}
				}
			}
		}
		if (x == ux+1) continue;
		for (x = ux; x >= lx; --x) {
			voxelInt = x*_size2 + y*_size + z;
			tmp = (_voxels.get())[voxelInt/BATCH_SIZE].load();
			if (GETBIT(tmp, voxelInt)) break;
			else {
				for (int i = 0; i < 4; ++i) {
					ny = y+DI_4[i][0]; nz = z+DI_4[i][1];
					if (nz>=lz && nz<=uz && ny>=ly && ny<=uy) {
						voxelInt = x*_size2 + ny*_size + nz;
						tmp = (_voxels.get())[voxelInt/BATCH_SIZE].load() | (_voxelsBuffer.get())[voxelInt/BATCH_SIZE].load();
						if (!GETBIT(tmp, voxelInt)) _bfsSolid(voxelInt);
					}
				}
			}
		}
	}
}

inline void Voxelizer::_fillXY(const int z) {
	int ly = (*_meshVoxLB)[1], uy = (*_meshVoxUB)[1], lx = (*_meshVoxLB)[0], ux = (*_meshVoxUB)[0];
	unsigned int voxelInt, tmp;
	for (int x = lx, y; x <= ux; ++x) {
		for (y = ly; y <= uy; ++y) {
			voxelInt = x*_size2 + y*_size + z;
			tmp = (_voxels.get())[voxelInt/BATCH_SIZE].load();
			if (GETBIT(tmp, voxelInt)) break;
			else {
				(_voxelsBuffer.get())[voxelInt / BATCH_SIZE] |= (1<< (voxelInt % BATCH_SIZE));
			}
		}
		if (y == uy+1) continue;
		for (y = uy; y >= ly; --y) {
			voxelInt = x*_size2 + y*_size + z;
			tmp = (_voxels.get())[voxelInt/BATCH_SIZE].load();
			if (GETBIT(tmp, voxelInt)) break;
			else {
				(_voxelsBuffer.get())[voxelInt / BATCH_SIZE] |= (1<< (voxelInt % BATCH_SIZE));
			}
		}
	}
}

inline void Voxelizer::_fillXY2(const int z) {
	int lx = (*_meshVoxLB)[0], ux = (*_meshVoxUB)[0], ly = (*_meshVoxLB)[1], uy = (*_meshVoxUB)[1], lz = (*_meshVoxLB)[2], uz = (*_meshVoxUB)[2], nx, nz;
	unsigned int voxelInt, tmp;
	for (int x = lx, y; x <= ux; ++x) {
		for (y = ly; y <= uy; ++y) {
			voxelInt = x*_size2 + y*_size + z;
			tmp = (_voxels.get())[voxelInt/BATCH_SIZE].load();
			if (GETBIT(tmp, voxelInt)) break;
			else {
				for (int i = 0; i < 4; ++i) {
					nx = x+DI_4[i][0]; nz = z+DI_4[i][1];
					if (nz>=lz && nz<=uz && nx>=lx && nx<=ux) {
						voxelInt = nx*_size2 + y*_size + nz;
						tmp = (_voxels.get())[voxelInt/BATCH_SIZE].load() | (_voxelsBuffer.get())[voxelInt/BATCH_SIZE].load();
						if (!GETBIT(tmp, voxelInt)) _bfsSolid(voxelInt);
					}
				}
			}
		}
		if (y == uy+1) continue;
		for (y = uy; y >= ly; --y) {
			voxelInt = x*_size2 + y*_size + z;
			tmp = (_voxels.get())[voxelInt/BATCH_SIZE].load();
			if (GETBIT(tmp, voxelInt)) break;
			else {
				for (int i = 0; i < 4; ++i) {
					nx = x+DI_4[i][0]; nz = z+DI_4[i][1];
					if (nz>=lz && nz<=uz && nx>=lx && nx<=ux) {
						voxelInt = nx*_size2 + y*_size + nz;
						tmp = (_voxels.get())[voxelInt/BATCH_SIZE].load() | (_voxelsBuffer.get())[voxelInt/BATCH_SIZE].load();
						if (!GETBIT(tmp, voxelInt)) _bfsSolid(voxelInt);
					}
				}
			}
		}
	}
}

inline void Voxelizer::_runSolidTask(size_t numThread) {
	ThreadPool tp(numThread);
	int lx = (*_meshVoxLB)[0], ux = (*_meshVoxUB)[0], ly = (*_meshVoxLB)[1], uy = (*_meshVoxUB)[1], lz = (*_meshVoxLB)[2], uz = (*_meshVoxUB)[2];
	for (int x = lx; x <= ux; ++x) {
		tp.run(boost::bind(&Voxelizer::_fillYZ, this, x));
	}
	for (int y = ly; y <= uy; ++y) {
		tp.run(boost::bind(&Voxelizer::_fillXZ, this, y));
	}
	for (int z = lz; z <= uz; ++z) {
		tp.run(boost::bind(&Voxelizer::_fillXY, this, z));
	}
	tp.stop();
}


inline void Voxelizer::_runSolidTask2(size_t numThread) {
	ThreadPool tp(numThread);
	int lx = (*_meshVoxLB)[0], ux = (*_meshVoxUB)[0], ly = (*_meshVoxLB)[1], uy = (*_meshVoxUB)[1], lz = (*_meshVoxLB)[2], uz = (*_meshVoxUB)[2];
	for (int x = lx; x <= ux; ++x) {
		tp.run(boost::bind(&Voxelizer::_fillYZ2, this, x));
	}
	for (int z = lz; z <= uz; ++z) {
		tp.run(boost::bind(&Voxelizer::_fillXY2, this, z));
	}
	for (int y = ly; y <= uy; ++y) {
		tp.run(boost::bind(&Voxelizer::_fillXZ2, this, y));
	}
	tp.stop();
}


/**
 * Write to file, with simple compression
 */
void Voxelizer::write(const string& pFile) {

	cout << "writing voxels to file..." << endl;
	int lx = (*_meshVoxLB)[0], ux = (*_meshVoxUB)[0], ly = (*_meshVoxLB)[1], uy = (*_meshVoxUB)[1], lz = (*_meshVoxLB)[2], uz = (*_meshVoxUB)[2];
	ofstream* output = new ofstream(pFile.c_str(), ios::out | ios::binary);

	//
	// write header
	//
	*output << _size;
	*output << (double) (*_lb)[0] << (double) (*_lb)[1] << (double) (*_lb)[2];;
	*output << (double) (*_halfUnit)[0] * 2;
	*output << lx << ly << lz << uz << uy << uz;

	cout << "grid size : " << _size << endl;
	cout << "lower bound : " << (*_lb)[0] << " " << (*_lb)[1] << " " << (*_lb)[2] << endl;
	cout << "voxel size : " << (*_halfUnit)[0] * 2 << endl;
	cout << "voxel bound : (" << lx << " " << ly << " " << lz << "), " << " (" << ux << " " << uy << " " << uz << ")" << endl;

	//
	// write data
	//
	/**
		 * Compression
		 */
	int x = lx, y = ly, z = lz, index, totalOnes = 0;
	byte value, count;
	while (x <= ux) {
		index = x*_size2 + y*_size + z;
		value = GETBIT(_voxels.get()[index/BATCH_SIZE],index) ;
		count = 0;
		while ((x <= ux) && (count < 255) && (value == GETBIT(_voxels.get()[index/BATCH_SIZE],index))) {
			z++;
			if (z > uz) {
				z = lz;
				y++;
				if (y > uy) {
					y = ly;
					x++;
				}
			}
			index = x*_size2 + y*_size + z;
			count++;
		}
		if (value)
			totalOnes += count;
		*output << value << count;
	}

	output->close();
	cout << "wrote " << totalOnes << " voxels" << endl;
}

/**
 * Write to file, with simple compression
 */
void Voxelizer::writeForView(const string& pFile) {

	cout << "writing voxels to file..." << endl;

	v3_p vxlBox(new Vec3f(0, 0, 0));
	int lx = 0, ux = _size - 1, ly = 0, uy = _size - 1, lz = 0, uz = _size - 1;
	int bx = ux-lx+1, by = uy-ly+1, bz = uz-lz+1;
	int meshLx = (*_meshVoxLB)[0], meshLy = (*_meshVoxLB)[1], meshLz = (*_meshVoxLB)[2];
	int meshUx = (*_meshVoxUB)[0], meshUy = (*_meshVoxUB)[1], meshUz = (*_meshVoxUB)[2];

	ofstream* output = new ofstream(pFile.c_str(), ios::out | ios::binary);

	Vec3f& norm_translate = (*_lb);
	float norm_scale = (*_bound).norm();

	//
	// write header
	//
	*output << "#binvox 1" << endl;
	*output << "dim " << bx  << " " << by << " " << bz << endl;
	cout << "dim : " << bx << " x " << by << " x " << bz << endl;
	*output << "translate " << -norm_translate[0] << " " << -norm_translate[2]
			<< " " << -norm_translate[1] << endl;
	*output << "scale " << norm_scale << endl;
	*output << "data" << endl;

	byte value;
	byte count;
	int index = 0;
	int bytes_written = 0;
	int total_ones = 0;

	/**
	 * Compression
	 */
	int x = lx, y = ly, z = lz;
	while (x <= ux) {
		index = x*_size2 + y*_size + z;
		value = _inRange(x, y, z, meshLx, meshLy, meshLz, meshUx, meshUy, meshUz) ? GETBIT(_voxels.get()[index/BATCH_SIZE],index) : 0;
		count = 0;
		while ((x <= ux) && (count < 255) && (value == (_inRange(x, y, z, meshLx, meshLy, meshLz, meshUx, meshUy, meshUz) ? GETBIT(_voxels.get()[index/BATCH_SIZE],index) : 0))) {
			z++;
			if (z > uz) {
				z = lz;
				y++;
				if (y > uy) {
					y = ly;
					x++;
				}
			}
			index = x*_size2 + y*_size + z;
			count++;
		}
		if (value)
			total_ones += count;
		*output << value << count;
		bytes_written += 2;
	}

	output->close();
	cout << "wrote " << total_ones << " set voxels out of " << bx*by*bz << ", in "
			<< bytes_written << " bytes" << endl;
}

void Voxelizer::writeSimple(const string& pFile) {
	cout << "writing voxels to file..." << endl;
	int lx = (*_meshVoxLB)[0], ux = (*_meshVoxUB)[0], ly = (*_meshVoxLB)[1], uy = (*_meshVoxUB)[1], lz = (*_meshVoxLB)[2], uz = (*_meshVoxUB)[2];
	ofstream* output = new ofstream(pFile.c_str(), ios::out | ios::binary);

	//
	// write header
	//
	*output << _size << endl;
	*output << (double) (*_lb)[0] << " " << (double) (*_lb)[1] << " " << (double) (*_lb)[2] << endl;
	*output << (double) (*_halfUnit)[0] * 2 << endl;

	cout << "dim : " << _size << " x " << _size << " x " << _size << endl;
	cout << "lower bound : " << (*_lb) << endl;
	cout << "voxel size : " << (*_halfUnit)[0] * 2 << endl;

	//
	// write data
	//
	unsigned int voxelInt, tmp, count = 0;
	for (int x = lx; x <= ux; ++x) {
		for (int y = ly; y <= uy; ++y) {
			for (int z = lz; z <= uz; ++z) {
				voxelInt = x*_size2 + y*_size + z;
				tmp = (_voxels.get())[voxelInt/BATCH_SIZE].load();
				if (GETBIT(tmp, voxelInt)) {
					*output << x << ' ' << y << ' ' << z << '\n';
//					if (count == 0) cout << x << " " << y << " " << z << endl;
					++count;
				}
			}
		}
	}
	output->close();
	cout << "wrote " << count << " voxels" << endl;
}

Voxelizer::~Voxelizer() {
	// TODO Auto-generated destructor stub
}


int main(int args, char* argv[]) {
	Timer timer;
//	string fileName = "/Users/chenqian/workspace/mujin/data/kawada-hironx.stl";
//	string fileName2 = "/Users/chenqian/workspace/mujin/data/test.binvox";
	if (args == 5) {
		int gridSize = atoi(argv[1]);
		int numThread = atoi(argv[2]);
		string inputFile = argv[3];
		string outputFile = argv[4];
		timer.restart();
		Voxelizer voxelizer(gridSize, inputFile);
		timer.stop();
		cout << "voxelizer initialization "; timer.printTimeInS();
		cout << "-------------------------------------------" << endl;
		timer.restart();
		voxelizer.voxelizeSurface(numThread);
		timer.stop();
		cout << "surface voxelization "; timer.printTimeInS();
		cout << "-------------------------------------------" << endl;
		timer.restart();
		voxelizer.voxelizeSolid(numThread);
		timer.stop();
		cout << "solid voxelization "; timer.printTimeInS();
		cout << "-------------------------------------------" << endl;
		timer.restart();
		voxelizer.writeSimple(outputFile);
//		voxelizer.write(outputFile);
//		voxelizer.writeForView(outputFile);
		timer.stop();
		cout << "writing file "; timer.printTimeInS();
		cout << "-------------------------------------------" << endl;
	} else {
		cout << "grid_size num_threads STL_file output_file" << endl;
	}
}
