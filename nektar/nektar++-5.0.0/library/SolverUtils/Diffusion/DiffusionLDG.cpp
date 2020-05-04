///////////////////////////////////////////////////////////////////////////////
//
// File: DiffusionLDG.cpp
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
// Description: LDG diffusion class.
//
///////////////////////////////////////////////////////////////////////////////

#include <iomanip>
#include <iostream>

#include <boost/algorithm/string/predicate.hpp>

#include <SolverUtils/Diffusion/DiffusionLDG.h>

namespace Nektar
{
namespace SolverUtils
{
std::string DiffusionLDG::type =
    GetDiffusionFactory().RegisterCreatorFunction("LDG", DiffusionLDG::create);

DiffusionLDG::DiffusionLDG()
{
}

void DiffusionLDG::v_InitObject(
    LibUtilities::SessionReaderSharedPtr pSession,
    Array<OneD, MultiRegions::ExpListSharedPtr> pFields)
{
    m_session = pSession;

    m_session->LoadSolverInfo("ShockCaptureType", m_shockCaptureType, "Off");

    // Set up penalty term for LDG
    m_session->LoadParameter("LDGc11", m_C11, 1.0);

    // Setting up the normals
    std::size_t nDim      = pFields[0]->GetCoordim(0);
    std::size_t nTracePts = pFields[0]->GetTrace()->GetTotPoints();

    m_traceNormals = Array<OneD, Array<OneD, NekDouble>>{nDim};
    for (std::size_t i = 0; i < nDim; ++i)
    {
        m_traceNormals[i] = Array<OneD, NekDouble>{nTracePts};
    }
    pFields[0]->GetTrace()->GetNormals(m_traceNormals);
}

void DiffusionLDG::v_Diffuse(
    const std::size_t nConvectiveFields,
    const Array<OneD, MultiRegions::ExpListSharedPtr> &fields,
    const Array<OneD, Array<OneD, NekDouble>> &inarray,
    Array<OneD, Array<OneD, NekDouble>> &outarray,
    const Array<OneD, Array<OneD, NekDouble>> &pFwd,
    const Array<OneD, Array<OneD, NekDouble>> &pBwd)
{
    std::size_t nDim      = fields[0]->GetCoordim(0);
    std::size_t nPts      = fields[0]->GetTotPoints();
    std::size_t nCoeffs   = fields[0]->GetNcoeffs();
    std::size_t nTracePts = fields[0]->GetTrace()->GetTotPoints();

    Array<OneD, NekDouble> tmp{nCoeffs};
    Array<OneD, Array<OneD, Array<OneD, NekDouble>>> flux{nDim};
    Array<OneD, Array<OneD, Array<OneD, NekDouble>>> qfield{nDim};

    for (std::size_t j = 0; j < nDim; ++j)
    {
        qfield[j] = Array<OneD, Array<OneD, NekDouble>>{nConvectiveFields};
        flux[j]   = Array<OneD, Array<OneD, NekDouble>>{nConvectiveFields};

        for (std::size_t i = 0; i < nConvectiveFields; ++i)
        {
            qfield[j][i] = Array<OneD, NekDouble>{nPts, 0.0};
            flux[j][i]   = Array<OneD, NekDouble>{nTracePts, 0.0};
        }
    }

    // Compute q_{\eta} and q_{\xi}
    // Obtain numerical fluxes

    NumFluxforScalar(fields, inarray, flux, pFwd, pBwd);

    for (std::size_t j = 0; j < nDim; ++j)
    {
        for (std::size_t i = 0; i < nConvectiveFields; ++i)
        {
            fields[i]->IProductWRTDerivBase(j, inarray[i], tmp);
            Vmath::Neg(nCoeffs, tmp, 1);
            fields[i]->AddTraceIntegral(flux[j][i], tmp);
            fields[i]->SetPhysState(false);
            fields[i]->MultiplyByElmtInvMass(tmp, tmp);
            fields[i]->BwdTrans(tmp, qfield[j][i]);
        }
    }

    // Initialize viscous tensor
    Array<OneD, Array<OneD, Array<OneD, NekDouble>>> viscTensor{nDim};
    for (std::size_t j = 0; j < nDim; ++j)
    {
        viscTensor[j] = Array<OneD, Array<OneD, NekDouble>>{nConvectiveFields};
        for (std::size_t i = 0; i < nConvectiveFields; ++i)
        {
            viscTensor[j][i] = Array<OneD, NekDouble>{nPts, 0.0};
        }
    }
    // Get viscous tensor
    m_fluxVector(inarray, qfield, viscTensor);

    // Compute u from q_{\eta} and q_{\xi}
    // Obtain numerical fluxes
    NumFluxforVector(fields, inarray, viscTensor, flux[0]);

    Array<OneD, Array<OneD, NekDouble>> qdbase{nDim};
    for (std::size_t i = 0; i < nConvectiveFields; ++i)
    {
        for (std::size_t j = 0; j < nDim; ++j)
        {
            qdbase[j] = viscTensor[j][i];
        }
        fields[i]->IProductWRTDerivBase(qdbase, tmp);

        // Evaulate  <\phi, \hat{F}\cdot n> - outarray[i]
        Vmath::Neg(nCoeffs, tmp, 1);
        fields[i]->AddTraceIntegral(flux[0][i], tmp);
        fields[i]->SetPhysState(false);
        fields[i]->MultiplyByElmtInvMass(tmp, tmp);
        fields[i]->BwdTrans(tmp, outarray[i]);
    }
}

void DiffusionLDG::NumFluxforScalar(
    const Array<OneD, MultiRegions::ExpListSharedPtr> &fields,
    const Array<OneD, Array<OneD, NekDouble>> &ufield,
    Array<OneD, Array<OneD, Array<OneD, NekDouble>>> &uflux,
    const Array<OneD, Array<OneD, NekDouble>> &pFwd,
    const Array<OneD, Array<OneD, NekDouble>> &pBwd)
{
    std::size_t nTracePts  = fields[0]->GetTrace()->GetTotPoints();
    std::size_t nvariables = fields.num_elements();
    std::size_t nDim       = fields[0]->GetCoordim(0);

    Array<OneD, NekDouble> Fwd{nTracePts};
    Array<OneD, NekDouble> Bwd{nTracePts};
    Array<OneD, NekDouble> fluxtemp{nTracePts, 0.0};

    // Get the sign of (v \cdot n), v = an arbitrary vector
    // Evaluate upwind flux:
    // uflux = \hat{u} \phi \cdot u = u^{(+,-)} n
    for (std::size_t i = 0; i < nvariables; ++i)
    {
        // Compute Fwd and Bwd value of ufield of i direction
        if (pFwd == NullNekDoubleArrayofArray ||
            pBwd == NullNekDoubleArrayofArray)
        {
            fields[i]->GetFwdBwdTracePhys(ufield[i], Fwd, Bwd);
        }
        else
        {
            Fwd = pFwd[i];
            Bwd = pBwd[i];
        }

        // Upwind
        Vmath::Vcopy(nTracePts, Fwd, 1, fluxtemp, 1);

        // Imposing weak boundary condition with flux
        if (fields[0]->GetBndCondExpansions().num_elements())
        {
            ApplyScalarBCs(fields, i, ufield[i], Fwd, Bwd, fluxtemp);
        }

        for (std::size_t j = 0; j < nDim; ++j)
        {
            Vmath::Vmul(nTracePts, m_traceNormals[j], 1, fluxtemp, 1,
                        uflux[j][i], 1);
        }
    }
}

void DiffusionLDG::ApplyScalarBCs(
    const Array<OneD, MultiRegions::ExpListSharedPtr> &fields,
    const std::size_t var, const Array<OneD, const NekDouble> &ufield,
    const Array<OneD, const NekDouble> &Fwd,
    const Array<OneD, const NekDouble> &Bwd,
    Array<OneD, NekDouble> &penaltyflux)
{
    boost::ignore_unused(ufield, Bwd);
    // Number of boundary regions
    std::size_t nBndRegions =
        fields[var]->GetBndCondExpansions().num_elements();
    std::size_t cnt = 0;
    for (std::size_t i = 0; i < nBndRegions; ++i)
    {
        if (fields[var]->GetBndConditions()[i]->GetBoundaryConditionType() ==
            SpatialDomains::ePeriodic)
        {
            continue;
        }

        // Number of boundary expansion related to that region
        std::size_t nBndEdges =
            fields[var]->GetBndCondExpansions()[i]->GetExpSize();

        // Weakly impose boundary conditions by modifying flux values
        for (std::size_t e = 0; e < nBndEdges; ++e)
        {
            std::size_t nBndEdgePts = fields[var]
                                           ->GetBndCondExpansions()[i]
                                           ->GetExp(e)
                                           ->GetTotPoints();

            std::size_t id1 =
                fields[var]->GetBndCondExpansions()[i]->GetPhys_Offset(e);

            std::size_t id2 = fields[0]->GetTrace()->GetPhys_Offset(
                fields[0]->GetTraceMap()->GetBndCondTraceToGlobalTraceMap(
                    cnt++));

            // AV boundary conditions
            if ( boost::iequals(fields[var]->GetBndConditions()[i]->
                 GetUserDefined(),"Wall") ||
                 boost::iequals(fields[var]->GetBndConditions()[i]->
                 GetUserDefined(),"Symmetry") ||
                 boost::iequals(fields[var]->GetBndConditions()[i]->
                 GetUserDefined(),"WallViscous") ||
                 boost::iequals(fields[var]->GetBndConditions()[i]->
                 GetUserDefined(),"WallAdiabatic"))
            {
                Vmath::Vcopy(nBndEdgePts, &Fwd[id2], 1, &penaltyflux[id2], 1);
            }
            // For Dirichlet boundary condition: uflux = g_D
            else if (fields[var]
                    ->GetBndConditions()[i]
                    ->GetBoundaryConditionType() == SpatialDomains::eDirichlet)
            {
                Vmath::Vcopy(
                    nBndEdgePts,
                    &(fields[var]->GetBndCondExpansions()[i]->GetPhys())[id1],
                    1, &penaltyflux[id2], 1);
            }
            // For Neumann boundary condition: uflux = u+
            else if ((fields[var]->GetBndConditions()[i])
                         ->GetBoundaryConditionType() ==
                     SpatialDomains::eNeumann)
            {
                Vmath::Vcopy(nBndEdgePts, &Fwd[id2], 1, &penaltyflux[id2], 1);
            }
        }
    }
}

/**
 * @brief Build the numerical flux for the 2nd order derivatives
 * todo: add variable coeff and h dependence to penalty term
 */
void DiffusionLDG::NumFluxforVector(
    const Array<OneD, MultiRegions::ExpListSharedPtr> &fields,
    const Array<OneD, Array<OneD, NekDouble>> &ufield,
    Array<OneD, Array<OneD, Array<OneD, NekDouble>>> &qfield,
    Array<OneD, Array<OneD, NekDouble>> &qflux)
{
    std::size_t nTracePts  = fields[0]->GetTrace()->GetTotPoints();
    std::size_t nvariables = fields.num_elements();
    std::size_t nDim       = qfield.num_elements();

    Array<OneD, NekDouble> Fwd{nTracePts};
    Array<OneD, NekDouble> Bwd{nTracePts};
    Array<OneD, NekDouble> qFwd{nTracePts};
    Array<OneD, NekDouble> qBwd{nTracePts};
    Array<OneD, NekDouble> qfluxtemp{nTracePts, 0.0};
    Array<OneD, NekDouble> uterm{nTracePts};

    // Evaulate upwind flux:
    // qflux = \hat{q} \cdot u = q \cdot n - C_(11)*(u^+ - u^-)
    for (std::size_t i = 0; i < nvariables; ++i)
    {
        // Generate Stability term = - C11 ( u- - u+ )
        fields[i]->GetFwdBwdTracePhys(ufield[i], Fwd, Bwd);
        Vmath::Vsub(nTracePts, Fwd, 1, Bwd, 1, uterm, 1);
        Vmath::Smul(nTracePts, -m_C11, uterm, 1, uterm, 1);

        qflux[i] = Array<OneD, NekDouble>{nTracePts, 0.0};
        for (std::size_t j = 0; j < nDim; ++j)
        {
            //  Compute Fwd and Bwd value of ufield of jth direction
            fields[i]->GetFwdBwdTracePhys(qfield[j][i], qFwd, qBwd);

            // Downwind
            Vmath::Vcopy(nTracePts, qBwd, 1, qfluxtemp, 1);

            Vmath::Vmul(nTracePts, m_traceNormals[j], 1, qfluxtemp, 1,
                        qfluxtemp, 1);

            // Flux = {Fwd, Bwd} * (nx, ny, nz) + uterm * (nx, ny)
            Vmath::Vadd(nTracePts, uterm, 1, qfluxtemp, 1, qfluxtemp, 1);

            // Imposing weak boundary condition with flux
            if (fields[0]->GetBndCondExpansions().num_elements())
            {
                ApplyVectorBCs(fields, i, j, qfield[j][i], qFwd, qBwd,
                               qfluxtemp);
            }

            // q_hat \cdot n = (q_xi \cdot n_xi) or (q_eta \cdot n_eta)
            // n_xi = n_x * tan_xi_x + n_y * tan_xi_y + n_z * tan_xi_z
            // n_xi = n_x * tan_eta_x + n_y * tan_eta_y + n_z*tan_eta_z
            Vmath::Vadd(nTracePts, qfluxtemp, 1, qflux[i], 1, qflux[i], 1);
        }
    }
}

/**
 * Diffusion: Imposing weak boundary condition for q with flux
 *  uflux = g_D  on Dirichlet boundary condition
 *  uflux = u_Fwd  on Neumann boundary condition
 */
void DiffusionLDG::ApplyVectorBCs(
    const Array<OneD, MultiRegions::ExpListSharedPtr> &fields,
    const std::size_t var, const std::size_t dir,
    const Array<OneD, const NekDouble> &qfield,
    const Array<OneD, const NekDouble> &qFwd,
    const Array<OneD, const NekDouble> &qBwd,
    Array<OneD, NekDouble> &penaltyflux)
{
    boost::ignore_unused(qfield, qBwd);

    std::size_t nBndRegions =
        fields[var]->GetBndCondExpansions().num_elements();
    std::size_t cnt = 0;

    for (std::size_t i = 0; i < nBndRegions; ++i)
    {
        if (fields[var]->GetBndConditions()[i]->GetBoundaryConditionType() ==
            SpatialDomains::ePeriodic)
        {
            continue;
        }
        std::size_t nBndEdges =
            fields[var]->GetBndCondExpansions()[i]->GetExpSize();

        // Weakly impose boundary conditions by modifying flux values
        for (std::size_t e = 0; e < nBndEdges; ++e)
        {
            std::size_t nBndEdgePts = fields[var]
                                           ->GetBndCondExpansions()[i]
                                           ->GetExp(e)
                                           ->GetTotPoints();

            std::size_t id1 =
                fields[var]->GetBndCondExpansions()[i]->GetPhys_Offset(e);

            std::size_t id2 = fields[0]->GetTrace()->GetPhys_Offset(
                fields[0]->GetTraceMap()->GetBndCondTraceToGlobalTraceMap(
                    cnt++));

            // AV boundary conditions
            if ( boost::iequals(fields[var]->GetBndConditions()[i]->
                 GetUserDefined(),"Wall") ||
                 boost::iequals(fields[var]->GetBndConditions()[i]->
                 GetUserDefined(),"Symmetry") ||
                 boost::iequals(fields[var]->GetBndConditions()[i]->
                 GetUserDefined(),"WallViscous") ||
                 boost::iequals(fields[var]->GetBndConditions()[i]->
                 GetUserDefined(),"WallAdiabatic"))
            {
                Vmath::Zero(nBndEdgePts, &penaltyflux[id2], 1);
            }
            // For Dirichlet boundary condition:
            // qflux = q+ - C_11 (u+ -    g_D) (nx, ny)
            else if (fields[var]
                    ->GetBndConditions()[i]
                    ->GetBoundaryConditionType() == SpatialDomains::eDirichlet)
            {
                Vmath::Vmul(nBndEdgePts, &m_traceNormals[dir][id2], 1,
                            &qFwd[id2], 1, &penaltyflux[id2], 1);
            }
            // For Neumann boundary condition: qflux = g_N
            else if ((fields[var]->GetBndConditions()[i])
                         ->GetBoundaryConditionType() ==
                     SpatialDomains::eNeumann)
            {
                Vmath::Vmul(
                    nBndEdgePts, &m_traceNormals[dir][id2], 1,
                    &(fields[var]->GetBndCondExpansions()[i]->GetPhys())[id1],
                    1, &penaltyflux[id2], 1);
            }
        }
    }
}

} // namespace SolverUtils
} // namespace Nektar