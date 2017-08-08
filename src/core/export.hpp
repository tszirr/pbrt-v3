#include <unordered_map>
#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace pbrt {

void Scene::exportRayn(char const* exportDir, Camera const* camera) const {
	struct V : PrimitiveVisitor {
		FILE* fdesc;
		std::string meshFile;
		std::string meshPath;

		V(char const* dir, Camera const* camera) {
			std::string prefix = dir;
			if (!prefix.empty() && prefix.back() != '/' && prefix.back() != '\\')
				prefix.push_back('/');
#ifdef _WIN32
			std::replace(prefix.begin(), prefix.end(), '/', '\\');
			system(("mkdir " + prefix).c_str());
			std::replace(prefix.begin(), prefix.end(), '\\', '/');
#else
			system(("mkdir -p " + prefix).c_str());
#endif
			std::string s = prefix + "scene.json";
			fdesc = fopen(s.c_str(), "wb+");
			assert(fdec);

			if (camera) {
				fputs("{ camera/pinhole:,\n", fdesc);
				serialize(fdesc, camera->CameraToWorld);
				// todo: camera parameters?
				fputs("},\n\n", fdesc);
			}

			meshPath = prefix + (meshFile = "everything.mesh");
		}
		~V() {
			fclose(fdesc);
		}

		// Parameter types
		static void serialize(FILE* fdesc, AnimatedTransform const& obj2world) {
			// todo: motion
			Transform t;
			obj2world.Interpolate(0, &t);
			serialize(fdesc, t);
		}
		static void serialize(FILE* fdesc, Transform const& obj2world) {
			Point3f pos = obj2world(Point3f(0, 0, 0));
			Vector3f up = obj2world(Vector3f(0, 1, 0));
			Vector3f dir = obj2world(Vector3f(0, 0, 1));
			fputs("\tlocation: {\n", fdesc);
			fprintf(fdesc, "\t\tposition: { x: %f, y: %f, z: %f },\n", pos.x, pos.y, pos.z);
			fprintf(fdesc, "\t\tdirection: { x: %f, y: %f, z: %f },\n", dir.x, dir.y, dir.z);
			fprintf(fdesc, "\t\tup: { x: %f, y: %f, z: %f },\n", up.x, up.y, up.z);
			fputs("\t},\n", fdesc);
		}

		// Meshes
		struct ExportedMesh {
			std::string file;
			std::vector<Transform> instances;
			size_t idx;
		};
		std::vector<TriangleMesh const*> orderedMeshes;
		std::unordered_map< TriangleMesh const*, ExportedMesh > exportedMeshes;
		
		TriangleMesh const* cachedM = nullptr;
		Transform cachedMT;

		virtual void visitMesh(Transform const& obj2world, TriangleMesh const* mesh, Material const* material, AreaLight const* light) {
			if (mesh == cachedM && obj2world == cachedMT)
				return;
			cachedM = mesh;
			cachedMT = obj2world;

			ExportedMesh& m = exportedMeshes[mesh];
			for (auto& t : m.instances)
				if (obj2world == t)
					return;

			// new mesh?
			if (m.instances.empty()) {
				m.idx = orderedMeshes.size();
				orderedMeshes.push_back(mesh);
			}
			m.instances.push_back(obj2world);

			fputs("{ shape/mesh:,\n", fdesc);
			fprintf(fdesc, "\tfile: %s,\n", meshFile.c_str());
			fprintf(fdesc, "\tindex: %d,\n", int(m.idx));
			serialize(fdesc, obj2world);

			// todo: bsdf, area light!

			fputs("},\n\n", fdesc);
		}
		void writeMeshes() {
			struct MeshHeader {
				size_t offset, count;
				int components;
				char type[16];
				char name[1024];
				char pad[64];
			};

			FILE* fmesh = fopen(meshPath.c_str(), "wb+");
			assert(fmesh);

			MeshHeader fileHeader = { };
			fileHeader.components = 0;
			for (TriangleMesh const* mesh : orderedMeshes) {
				int elements = 1 + 2 + !!mesh->n.get() + !!mesh->uv.get(); // pos, nrm, uv, face
				fileHeader.components += elements;
			}
			fileHeader.offset = (1 + fileHeader.components) * sizeof(MeshHeader);
			strcpy(fileHeader.type, "mesh");

			size_t headerCursor = fwrite(&fileHeader, 1, sizeof(MeshHeader), fmesh);
			size_t dataCursor = fileHeader.offset;
			for (TriangleMesh const* mesh : orderedMeshes) {
				fseek(fmesh, dataCursor, SEEK_SET);
				
				MeshHeader meshHeader = { dataCursor, 0, 0, "mesh" };

				MeshHeader vertexHeader = { dataCursor, mesh->nVertices, 3, "float", "vertex" };
				dataCursor += fwrite(mesh->p.get(), 1, sizeof(Point3f) * vertexHeader.count, fmesh);
				++meshHeader.components;

				MeshHeader normalHeader = { dataCursor, vertexHeader.count, 3, "float", "normal" };
				if (mesh->n.get()) {
					dataCursor += fwrite(mesh->n.get(), 1, sizeof(Normal3f) * normalHeader.count, fmesh);
					++meshHeader.components;
				}
				MeshHeader texHeader = { dataCursor, vertexHeader.count, 2, "float", "texcoord" };
				if (mesh->uv.get()) {
					dataCursor += fwrite(mesh->uv.get(), 1, sizeof(Point2f) * texHeader.count, fmesh);
					++meshHeader.components;
				}
				MeshHeader faceHeader = { dataCursor, mesh->nTriangles, 3, "int", "face" };
				dataCursor += fwrite(&mesh->vertexIndices[0], 1, sizeof(int) * faceHeader.components * faceHeader.count, fmesh);
				++meshHeader.components;

				meshHeader.count = dataCursor - meshHeader.offset;
				fileHeader.count += meshHeader.count;

				fseek(fmesh, headerCursor, SEEK_SET);
				headerCursor += fwrite(&meshHeader, 1, sizeof(MeshHeader), fmesh);
				headerCursor += fwrite(&vertexHeader, 1, sizeof(MeshHeader), fmesh);
				if (mesh->n.get())
					headerCursor += fwrite(&normalHeader, 1, sizeof(MeshHeader), fmesh);
				if (mesh->uv.get())
					headerCursor += fwrite(&texHeader, 1, sizeof(MeshHeader), fmesh);
				headerCursor += fwrite(&faceHeader, 1, sizeof(MeshHeader), fmesh);
			}

			assert(headerCursor == fileHeader.offset);
			fseek(fmesh, 0, SEEK_SET);
			fwrite(&fileHeader, 1, sizeof(MeshHeader), fmesh);

			fclose(fmesh);
		}
		
		virtual void visitLight(Transform const& obj2world, Light const* mesh) {

		}
	} v(exportDir, camera);
	aggregate->visit(0.5, Transform(), v);
	v.writeMeshes();
}

}  // namespace pbrt
