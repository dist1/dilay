#include "winged-mesh.hpp"
#include "winged-edge-iterator.hpp"

WingedEdgeIterator :: WingedEdgeIterator (const WingedFace& f) 
  : _face  (f)
  , _start (f.edge ())
  , _edge  (f.edge ())
  , _hasEdge (true)
  {}

LinkedEdge WingedEdgeIterator :: edge () {
  assert (this->_hasEdge);
  return this->_edge;
}

void WingedEdgeIterator :: next () {
  this->_edge = this->_edge->successor (this->_face);
  if (this->_edge == this->_start)
    this->_hasEdge = false;
}