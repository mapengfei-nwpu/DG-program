///////////////////////////////////////////////////////////////////////////////
//
// File: SegExp.cpp
//
// For more information, please see: http://www.nektar.info
//
// The MIT License
//
// Copyright (c) 2006 Division of Applied Mathematics, Brown University (USA),
// Department of Aeronautics, Imperial College London (UK), and Scientific
// Computing and Imaging Institute, University of Utah (USA).
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.
//
// Description: Python wrapper for SegExp.
//
///////////////////////////////////////////////////////////////////////////////

#include <LibUtilities/Python/NekPyConfig.hpp>
#include <LocalRegions/SegExp.h>

using namespace Nektar;
using namespace Nektar::LocalRegions;

void export_SegExp()
{
    py::class_<SegExp, py::bases<Expansion, StdRegions::StdSegExp>,
               std::shared_ptr<SegExp> >(
                   "SegExp", py::init<const LibUtilities::BasisKey&,
                   const SpatialDomains::Geometry1DSharedPtr &>());

    NEKPY_SHPTR_FIX(SegExp, Expansion);
    NEKPY_SHPTR_FIX(SegExp, StdRegions::StdSegExp);
    NEKPY_SHPTR_FIX(SegExp, StdRegions::StdExpansion);
}
