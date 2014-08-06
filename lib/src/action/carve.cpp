#define GLM_FORCE_RADIANS
#include <unordered_set>
#include <unordered_map>
#include <set>
#include <glm/glm.hpp>
#include <glm/gtx/norm.hpp>
#include "action/carve.hpp"
#include "id.hpp"
#include "action/unit/on.hpp"
#include "action/subdivide.hpp"
#include "partial-action/modify-winged-vertex.hpp"
#include "primitive/sphere.hpp"
#include "primitive/triangle.hpp"
#include "carve-brush.hpp"
#include "winged/face.hpp"
#include "winged/mesh.hpp"
#include "winged/vertex.hpp"
#include "adjacent-iterator.hpp"
#include "intersection.hpp"
#include "octree.hpp"

namespace {
  class VertexData {
    public: 
      VertexData (const glm::vec3& p, const glm::vec3& n)
        : position (p)
        , normal   (n)
        , carved   (false)
        , delta    (0.0f)
        {}

      const glm::vec3 position;
      const glm::vec3 normal;

      bool  wasCarved () const { return this->carved; }
      float getDelta  () const { return this->delta;  }

      void  setDelta (float d) { 
        this->delta = d;
        this->carved = true;
      }

    private:
      bool  carved;
      float delta;
  };
};

struct CarveCache::Impl {
  std::unordered_map <unsigned int,VertexData> vertexCache;
  Octree                                       faceCache;                   
  WingedMesh*                                  meshCache;

  Impl () : faceCache (true) {}

  VertexData& cacheVertex (const WingedMesh& mesh, const WingedVertex& vertex) {
    auto it = this->vertexCache.find (vertex.index ());

    if (it == this->vertexCache.end () || it->second.wasCarved () == false) {
      VertexData data (vertex.vertex (mesh), vertex.normal (mesh));
      return this->vertexCache.emplace (vertex.index (), data).first->second;
    }
    else {
      return it->second;
    }
  }

  void cacheFace (const WingedFace& face) {
    if (this->faceCache.hasFace (face.id ()) == false) {
      PrimTriangle triangle ( this->cachedVertex (face.firstVertex  ()).position
                            , this->cachedVertex (face.secondVertex ()).position
                            , this->cachedVertex (face.thirdVertex  ()).position
                            );
      this->faceCache.insertFace (face, triangle);
    }
  }

  VertexData& cachedVertex (const WingedVertex& vertex) {
    auto it = this->vertexCache.find (vertex.index ());

    if (it == this->vertexCache.end ()) {
      assert (false);
    }
    else {
      return it->second;
    }
  }

  bool intersects (const PrimRay& ray, Intersection& intersection) {
    return this->faceCache.intersects (ray, intersection);
  }

  void reset () {
    this->vertexCache.clear ();
    this->faceCache  .reset ();
    this->meshCache = nullptr;
  }
};

DELEGATE_BIG3 (CarveCache)
DELEGATE2     (bool, CarveCache, intersects, const PrimRay&, Intersection&)
GETTER        (WingedMesh*, CarveCache, meshCache)
SETTER        (WingedMesh*, CarveCache, meshCache)
DELEGATE      (void, CarveCache, reset)

struct ActionCarve::Impl {
  ActionCarve*              self;
  ActionUnitOn <WingedMesh> actions;

  Impl (ActionCarve* s) : self (s) {
    self->writeMesh  (true);
    self->bufferMesh (true);
  }

  void runUndoBeforePostProcessing (WingedMesh& mesh) { this->actions.undo (mesh); }
  void runRedoBeforePostProcessing (WingedMesh& mesh) { this->actions.redo (mesh); }

  void run ( WingedMesh& mesh, const glm::vec3& position, float width, CarveCache& cache) { 
    CarveBrush              brush (width, 0.05f);
    std::unordered_set <Id> ids;
    PrimSphere              sphere (position, width);

    mesh.intersects (sphere, ids);

    this->subdivideFaces (mesh, sphere, brush, ids, *cache.impl);
    this->cacheFaces     (mesh, ids, *cache.impl);
    this->carveFaces     (mesh, position, brush, ids, *cache.impl);

    /*
    // FOR DEBUGGING
    for (const Id& id : ids) {
      WingedFace* face = mesh.face (id);
      if (face && IntersectionUtil::intersects (sphere,mesh,*face)) {
        assert (this->isSubdividable (mesh,*face) == false);
      }
    }
    */

    mesh.write ();
    mesh.bufferData ();
  }

  bool isSubdividable ( const WingedMesh& mesh, const glm::vec3& poa, const CarveBrush& brush
                      , const WingedFace& face, CarveCache::Impl& cache)
  {
    const float       threshold = 0.03f;
    const VertexData& d1        = cache.cacheVertex (mesh, face.firstVertex  ());
    const VertexData& d2        = cache.cacheVertex (mesh, face.secondVertex ());
    const VertexData& d3        = cache.cacheVertex (mesh, face.thirdVertex  ());
    const glm::vec3   v1        = this->carvedVertex (poa, brush, d1);
    const glm::vec3   v2        = this->carvedVertex (poa, brush, d2);
    const glm::vec3   v3        = this->carvedVertex (poa, brush, d3);

    const float maxEdgeLength = glm::max ( glm::distance2 (v1, v2)
                                         , glm::max ( glm::distance2 (v1, v3)
                                                    , glm::distance2 (v2, v3)));

    return maxEdgeLength > threshold * threshold;
  }

  bool isSubdividable (const WingedMesh& mesh, const WingedFace& face) const
  {
    const float       threshold = 0.03f;
    const glm::vec3   v1        = face.firstVertex ().vertex (mesh);
    const glm::vec3   v2        = face.secondVertex ().vertex (mesh);
    const glm::vec3   v3        = face.thirdVertex ().vertex (mesh);

    const float maxEdgeLength = glm::max ( glm::distance2 (v1, v2)
                                         , glm::max ( glm::distance2 (v1, v3)
                                                    , glm::distance2 (v2, v3)));

    return maxEdgeLength > threshold * threshold;
  }

  float delta (const glm::vec3& poa, const CarveBrush& brush, const VertexData& vd) const {
    return brush.y (glm::distance <float> (vd.position, poa));
  }

  glm::vec3 carvedVertex (const glm::vec3& poa, const CarveBrush& brush, const VertexData& vd) const {
    return vd.position + (vd.normal * this->delta (poa, brush, vd));
  }

  glm::vec3 carveVertex (const glm::vec3& poa, const CarveBrush& brush, VertexData& vd) const {
    vd.setDelta (glm::max (vd.getDelta (), this->delta (poa, brush, vd)));

    return vd.position + (vd.normal * vd.getDelta ());
  }

  void subdivideFaces ( WingedMesh& mesh, const PrimSphere& sphere, const CarveBrush& brush
                      , std::unordered_set <Id>& ids, CarveCache::Impl& cache)
  {
    std::unordered_set <Id> thisIteration = ids;
    std::unordered_set <Id> nextIteration;

    auto checkNextIteration = [&] (const WingedFace& face) -> void {
      if ( nextIteration.count (face.id ()) == 0 
        && this->isSubdividable (mesh, sphere.center (), brush, face, cache)
        && IntersectionUtil::intersects (sphere,mesh,face)) 
      {
        nextIteration.insert (face.id ());
      }
    };

    while (thisIteration.size () > 0) {
      for (const Id& id : thisIteration) {
        WingedFace* f = mesh.face (id);
        if (f && this->isSubdividable (mesh, sphere.center (), brush, *f, cache)) {
          std::list <Id> affectedFaces;
          this->actions.add <ActionSubdivide> ().run (mesh, *f, &affectedFaces);

          for (Id& id : affectedFaces) {
            WingedFace* affected = mesh.face (id);
            if (affected) {
              checkNextIteration (*affected);
              ids.insert (affected->id ());
            }
          }
        }
      }
      thisIteration = nextIteration;
      nextIteration.clear ();
    }
  }

  void cacheFaces ( WingedMesh& mesh, const std::unordered_set <Id>& ids
                  , CarveCache::Impl& cache) 
  {
    for (const Id& id : ids) {
      WingedFace* face = mesh.face (id);
      if (face) {
        cache.cacheFace (*face);
      }
    }
  }

  void carveFaces ( WingedMesh& mesh, const glm::vec3& poa, const CarveBrush& brush
                  , const std::unordered_set <Id>& ids, CarveCache::Impl& cache) 
  {
    // Compute set of vertices
    std::set <WingedVertex*> vertices;
    for (const Id& id : ids) {
      WingedFace* face = mesh.face (id);
      if (face) {
        vertices.insert (&face->firstVertex  ());
        vertices.insert (&face->secondVertex ());
        vertices.insert (&face->thirdVertex  ());
      }
    }

    // Compute new positions
    std::list <glm::vec3> newPositions;
    for (WingedVertex* v : vertices) {
      newPositions.push_back (this->carveVertex (poa, brush, cache.cachedVertex (*v)));
    }

    // Write new positions
    assert (newPositions.size () == vertices.size ());
    auto newPosition = newPositions.begin ();
    for (WingedVertex* v : vertices) {
      this->actions.add <PAModifyWVertex> ().move (mesh,*v,*newPosition);
      ++newPosition;
    }

    // Write normals
    for (WingedVertex* v : vertices) {
      this->actions.add <PAModifyWVertex> ().writeNormal (mesh,*v);
    }

    // Realign faces.
    for (const Id& id : ids) {
      WingedFace* face = mesh.face (id);
      if (face) {
        this->self->realignFace (mesh, *face);
      }
    }
  }
};

DELEGATE_BIG3_SELF (ActionCarve)
DELEGATE4          (void, ActionCarve, run , WingedMesh&, const glm::vec3&, float, CarveCache&)
DELEGATE1          (void, ActionCarve, runUndoBeforePostProcessing, WingedMesh&)
DELEGATE1          (void, ActionCarve, runRedoBeforePostProcessing, WingedMesh&)
