////////////////////////////////////////////////////////////////////////////////
//
//  File: SurfaceMeshing.cpp
//
//  For more information, please see: http://www.nektar.info/
//
//  The MIT License
//
//  Copyright (c) 2006 Division of Applied Mathematics, Brown University (USA),
//  Department of Aeronautics, Imperial College London (UK), and Scientific
//  Computing and Imaging Institute, University of Utah (USA).
//
//  Permission is hereby granted, free of charge, to any person obtaining a
//  copy of this software and associated documentation files (the "Software"),
//  to deal in the Software without restriction, including without limitation
//  the rights to use, copy, modify, merge, publish, distribute, sublicense,
//  and/or sell copies of the Software, and to permit persons to whom the
//  Software is furnished to do so, subject to the following conditions:
//
//  The above copyright notice and this permission notice shall be included
//  in all copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
//  OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
//  THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
//  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
//  DEALINGS IN THE SOFTWARE.
//
//  Description: surfacemeshing object methods.
//
////////////////////////////////////////////////////////////////////////////////

#include "CFIMesh.h"

#include <NekMeshUtils/CADSystem/CADCurve.h>
#include <NekMeshUtils/CADSystem/CADSurf.h>
#include <NekMeshUtils/CADSystem/CADVert.h>

using namespace std;
namespace Nektar
{
namespace NekMeshUtils
{

ModuleKey CFIMesh::className = GetModuleFactory().RegisterCreatorFunction(
    ModuleKey(eProcessModule, "cfimesh"), CFIMesh::create,
    "Extracts mesh from cfi");

CFIMesh::CFIMesh(MeshSharedPtr m) : ProcessModule(m)
{
}

CFIMesh::~CFIMesh()
{
}

void CFIMesh::Process()
{
    if (m_mesh->m_verbose)
    {
        cout << endl << "Loading mesh from CFI" << endl;
    }

    m_mesh->m_expDim   = 3;
    m_mesh->m_spaceDim = 3;

    m_cad           = std::dynamic_pointer_cast<CADSystemCFI>(m_mesh->m_cad);
    m_nameToCurveId = m_cad->GetCFICurveId();
    m_nameToFaceId  = m_cad->GetCFIFaceId();
    m_nameToVertId  = m_cad->GetCFIVertId();
    m_model         = m_cad->GetCFIModel();
    NekDouble scal  = m_cad->GetScaling();

    map<int, NodeSharedPtr> nodes;
    vector<cfi::NodeDefinition> *cfinodes = m_model->getFenodes();

    if (m_mesh->m_verbose)
    {
        cout << "Nodes " << cfinodes->size() << endl;
    }

    // filter all mesh nodes into a indexed map and project to CAD
    for (auto &it : *cfinodes)
    {
        Array<OneD, NekDouble> xyz(3);
        cfi::Position ps = it.node->getXYZ();
        xyz[0]           = ps.x * scal;
        xyz[1]           = ps.y * scal;
        xyz[2]           = ps.z * scal;
        int id           = it.node->number;

        NodeSharedPtr n =
            std::shared_ptr<Node>(new Node(id, xyz[0], xyz[1], xyz[2]));
        nodes.insert(pair<int, NodeSharedPtr>(id, n));

        // point built now add cad if needed
        cfi::MeshableEntity *p = it.parent;

        if (p->type == cfi::TYPE_LINE)
        {
            auto f = m_nameToCurveId.find(p->getName());
            if (f != m_nameToCurveId.end())
            {
                CADCurveSharedPtr c = m_mesh->m_cad->GetCurve(f->second);
                NekDouble t;
                c->loct(xyz, t);
                n->SetCADCurve(c, t);

                vector<pair<CADSurfSharedPtr, CADOrientation::Orientation>> ss =
                    c->GetAdjSurf();
                for (int j = 0; j < ss.size(); j++)
                {
                    Array<OneD, NekDouble> uv = ss[j].first->locuv(xyz);
                    n->SetCADSurf(ss[j].first, uv);
                }
            }
        }
        else if (p->type == cfi::TYPE_FACE)
        {
            auto f = m_nameToFaceId.find(p->getName());
            if (f != m_nameToFaceId.end())
            {
                CADSurfSharedPtr s = m_mesh->m_cad->GetSurf(f->second);
                Array<OneD, NekDouble> uv = s->locuv(xyz);
                n->SetCADSurf(s, uv);
            }
        }
        else if (p->type == cfi::TYPE_POINT)
        {
            auto f = m_nameToVertId.find(p->getName());
            if (f != m_nameToVertId.end())
            {
                CADVertSharedPtr v = m_mesh->m_cad->GetVert(f->second);
                vector<CADCurveSharedPtr> cs = v->GetAdjCurves();
                for (int i = 0; i < cs.size(); i++)
                {
                    NekDouble t;
                    cs[i]->loct(xyz, t);
                    n->SetCADCurve(cs[i], t);
                    vector<pair<CADSurfSharedPtr, CADOrientation::Orientation>>
                        ss = cs[i]->GetAdjSurf();
                    for (int j = 0; j < ss.size(); j++)
                    {
                        Array<OneD, NekDouble> uv = ss[j].first->locuv(xyz);
                        n->SetCADSurf(ss[j].first, uv);
                    }
                }
            }
        }
    }

    ////
    // Really important fact. Nodes must be renumbered as they are read by the
    // elements
    // such that vertical edges on the prism are sequential
    // In doing so we ensure the orienation will work
    // we dont want to renumber nodes that have already been numbered, hence the
    // set
    // the set will be tacked by the cfiID as that is a constant

    map<int, set<LibUtilities::ShapeType>> cfiIdToTypes;

    vector<cfi::ElementDefinition> *prisms =
        m_model->getElements(cfi::SUBTYPE_PE6, 6);
    for (auto &it : *prisms)
    {
        vector<cfi::Node *> ns = it.nodes;
        for (int i = 0; i < ns.size(); i++)
        {
            cfiIdToTypes[ns[i]->number].insert(LibUtilities::ePrism);
        }
    }
    vector<cfi::ElementDefinition> *hexs =
        m_model->getElements(cfi::SUBTYPE_HE8, 8);
    for (auto &it : *hexs)
    {
        vector<cfi::Node *> ns = it.nodes;
        for (int i = 0; i < ns.size(); i++)
        {
            cfiIdToTypes[ns[i]->number].insert(LibUtilities::eHexahedron);
        }
    }
    vector<cfi::ElementDefinition> *tets =
        m_model->getElements(cfi::SUBTYPE_TE4, 4);
    for (auto &it : *tets)
    {
        vector<cfi::Node *> ns = it.nodes;
        for (int i = 0; i < ns.size(); i++)
        {
            cfiIdToTypes[ns[i]->number].insert(LibUtilities::eTetrahedron);
        }
    }

    WARNINGL0(nodes.size() == cfiIdToTypes.size(), "not all nodes marked");

    int id = 0;

    for (int i = 1; i <= 3; i++)
    {
        for (auto &it : cfiIdToTypes)
        {
            if (it.second.size() == i)
            {
                nodes[it.first]->m_id = id++;
            }
        }
    }

    WARNINGL0(id == nodes.size(), "not all nodes numbered");

    int prefix = m_mesh->m_cad->GetNumSurf() > 100 ? 1000 : 100;

    if (m_mesh->m_verbose)
    {
        cout << "prisms " << prisms->size() << endl;
    }

    int nm[6] = {3, 2, 5, 0, 1, 4};
    for (auto &it : *prisms)
    {
        vector<NodeSharedPtr> n(6);

        vector<cfi::Node *> ns = it.nodes;

        for (int j = 0; j < ns.size(); j++)
        {
            n[nm[j]] = nodes[ns[j]->number];
        }

        vector<int> tags;
        tags.push_back(prefix + 1);
        ElmtConfig conf(LibUtilities::ePrism, 1, false, false);

        ElementSharedPtr E = GetElementFactory().CreateInstance(
            LibUtilities::ePrism, conf, n, tags);

        m_mesh->m_element[3].push_back(E);
    }

    if (m_mesh->m_verbose)
    {
        cout << "tets " << tets->size() << endl;
    }

    for (auto &it : *tets)
    {
        vector<NodeSharedPtr> n;
        vector<cfi::Node *> ns = it.nodes;

        for (int j = 0; j < ns.size(); j++)
        {
            n.push_back(nodes[ns[j]->number]);
        }

        vector<int> tags;
        tags.push_back(prefix);
        ElmtConfig conf(LibUtilities::eTetrahedron, 1, false, false);
        ElementSharedPtr E = GetElementFactory().CreateInstance(
            LibUtilities::eTetrahedron, conf, n, tags);

        m_mesh->m_element[3].push_back(E);
    }

    if (m_mesh->m_verbose)
    {
        cout << "hexes " << hexs->size() << endl;
    }

    for (auto &it : *hexs)
    {
        vector<NodeSharedPtr> n;
        vector<cfi::Node *> ns = it.nodes;

        for (int j = 0; j < ns.size(); j++)
        {
            n.push_back(nodes[ns[j]->number]);
        }

        vector<int> tags;
        tags.push_back(prefix + 2);
        ElmtConfig conf(LibUtilities::eHexahedron, 1, false, false);
        ElementSharedPtr E = GetElementFactory().CreateInstance(
            LibUtilities::eHexahedron, conf, n, tags);

        m_mesh->m_element[3].push_back(E);
    }

    ProcessVertices();
    ProcessEdges();
    ProcessFaces();
    ProcessElements();
    ProcessComposites();

    vector<cfi::ElementDefinition> *tris =
        m_model->getElements(cfi::SUBTYPE_TR3, 3);
    if (m_mesh->m_verbose)
    {
        cout << "tris " << tris->size() << endl;
    }

    for (auto &it : *tris)
    {
        cfi::MeshableEntity *p = it.parent;
        auto f                 = m_nameToFaceId.find(p->getName());

        if (f != m_nameToFaceId.end())
        {
            vector<NodeSharedPtr> n;
            vector<cfi::Node *> ns = it.nodes;

            for (int j = 0; j < ns.size(); j++)
            {
                n.push_back(nodes[ns[j]->number]);
            }

            vector<int> tags;
            tags.push_back(f->second);
            ElmtConfig conf(LibUtilities::eTriangle, 1, false, false, false);
            ElementSharedPtr E = GetElementFactory().CreateInstance(
                LibUtilities::eTriangle, conf, n, tags);

            FaceSharedPtr fc = FaceSharedPtr(
                new Face(E->GetVertexList(), vector<NodeSharedPtr>(),
                         E->GetEdgeList(), LibUtilities::ePolyEvenlySpaced));

            FaceSet::iterator fnd = m_mesh->m_faceSet.find(fc);
            ASSERTL0(fnd != m_mesh->m_faceSet.end(),
                     "surface element not found in mesh");

            FaceSharedPtr mf = *fnd;

            if (mf->m_elLink.size() == 1)
            {
                E->m_parentCAD = m_mesh->m_cad->GetSurf(f->second);
                m_mesh->m_element[2].push_back(E);
            }
        }
    }

    vector<cfi::ElementDefinition> *quads =
        m_model->getElements(cfi::SUBTYPE_QU4, 4);
    if (m_mesh->m_verbose)
    {
        cout << "quads " << quads->size() << endl;
    }

    for (auto &it : *quads)
    {
        cfi::MeshableEntity *p = it.parent;
        auto f                 = m_nameToFaceId.find(p->getName());

        if (f != m_nameToFaceId.end())
        {
            vector<NodeSharedPtr> n;
            vector<cfi::Node *> ns = it.nodes;

            for (int j = 0; j < ns.size(); j++)
            {
                n.push_back(nodes[ns[j]->number]);
            }

            vector<int> tags;
            tags.push_back(f->second);
            ElmtConfig conf(LibUtilities::eQuadrilateral, 1, false, false,
                            false);
            ElementSharedPtr E = GetElementFactory().CreateInstance(
                LibUtilities::eQuadrilateral, conf, n, tags);

            FaceSharedPtr fc = FaceSharedPtr(
                new Face(E->GetVertexList(), vector<NodeSharedPtr>(),
                         E->GetEdgeList(), LibUtilities::ePolyEvenlySpaced));

            FaceSet::iterator fnd = m_mesh->m_faceSet.find(fc);
            ASSERTL0(fnd != m_mesh->m_faceSet.end(),
                     "surface element not found in mesh");

            FaceSharedPtr mf = *fnd;

            if (mf->m_elLink.size() == 1)
            {
                E->m_parentCAD = m_mesh->m_cad->GetSurf(f->second);
                m_mesh->m_element[2].push_back(E);
            }
        }
    }

    ProcessVertices();
    ProcessEdges();
    ProcessFaces();
    ProcessElements();
    ProcessComposites();

    // surface edges are different to mesh edges
    // first of all need to process them so they are unique
    // and then find CAD for them from beams

    EdgeSet surfaceEdges;
    for (int i = 0; i < m_mesh->m_element[2].size(); i++)
    {
        vector<EdgeSharedPtr> es = m_mesh->m_element[2][i]->GetEdgeList();
        for (int j = 0; j < es.size(); j++)
        {
            surfaceEdges.insert(es[j]);
        }
    }

    vector<cfi::ElementDefinition> *beams =
        m_model->getElements(cfi::SUBTYPE_BE2, 2);
    if (m_mesh->m_verbose)
    {
        cout << "beams " << beams->size() << endl;
    }

    for (auto &it : *beams)
    {
        cfi::MeshableEntity *p = it.parent;
        auto f                 = m_nameToCurveId.find(p->getName());

        if (f != m_nameToCurveId.end())
        {
            vector<NodeSharedPtr> n;
            vector<cfi::Node *> ns = it.nodes;

            for (int j = 0; j < ns.size(); j++)
            {
                n.push_back(nodes[ns[j]->number]);
            }

            // going to create a edge from the cfi element and find its
            // counterpart
            // in the m_mesh edgeset

            EdgeSharedPtr ec = EdgeSharedPtr(new Edge(n[0], n[1]));

            EdgeSet::iterator find = surfaceEdges.find(ec);
            if (find == surfaceEdges.end())
            {
                continue;
            }

            EdgeSharedPtr me = *find;

            me->m_parentCAD = m_mesh->m_cad->GetCurve(f->second);
        }
    }
}
}
}
