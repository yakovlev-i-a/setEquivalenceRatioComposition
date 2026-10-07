/*---------------------------------------------------------------------------*\
  =========                 |
  \\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox
   \\    /   O peration     |
    \\  /    A nd           | www.openfoam.com
     \\/     M anipulation  |
-------------------------------------------------------------------------------
    Copyright (C) 2025
-------------------------------------------------------------------------------
License
    This file is part of OpenFOAM.

    OpenFOAM is free software: you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    OpenFOAM is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
    for more details.

    You should have received a copy of the GNU General Public License
    along with OpenFOAM.  If not, see <http://www.gnu.org/licenses/>.

Application
    setEquivalenceRatioComposition

Description
    Set the initial (internal) field and the inlet patch value of the species
    mass fractions CH4, O2 and N2 from a prescribed equivalence ratio.

    The equivalence ratio and (optionally) the air composition / species molar
    masses are read from the dictionary:

        constant/<region>/phiEq   (if -region is given and the file exists)
        constant/phiEq            (otherwise)

    with the following entries:

        value            <phi>;          // equivalence ratio (required)
        kO2Air           <0.20946>;      // O2 mole fraction in air (optional)
        KfuelOxidizer    <0.5>;          // fuel/oxidizer stoichiometric ratio
                                         // (moles O2 per mole CH4) (optional)
        MassCH4          <16.0428>;      // kg/kmol (optional)
        MassO2           <31.9988>;      // kg/kmol (optional)
        MassN2           <28.0134>;      // kg/kmol (optional)

    The mass fractions are computed from the mole fractions:

        X_CH4 = KfuelOxidizer*kO2Air*phi / (KfuelOxidizer*kO2Air*phi + 1)
        X_O2  = (1 - X_CH4)*kO2Air
        X_N2  = 1 - X_CH4 - X_O2

    and converted to mass fractions via the mixture molar mass.

    The fields CH4, O2 and N2 are updated (in 0/ or in 0/<region>/):
      - internalField  -> uniform <Y>
      - inlet patch    -> fixedValue with value <Y>

    The inlet patch is auto-detected from a list of common names
    (inlet, in). It can be overridden with the command-line option
    -patch <name>. Use -listPatches to print all available patches.

    Use -region <name> to work on a named mesh region (multi-region cases).
    Without -region the default (single) region is used.

\*---------------------------------------------------------------------------*/

#include "argList.H"
#include "Time.H"
#include "fvMesh.H"
#include "volFields.H"
#include "dictionary.H"
#include "IFstream.H"
#include "OSspecific.H"
#include "fvPatchField.H"

using namespace Foam;

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

int main(int argc, char *argv[])
{
    argList::addNote
    (
        "Set CH4/O2/N2 mass fractions from an equivalence ratio "
        "(constant/phiEq or constant/<region>/phiEq)."
    );

    argList::addOption
    (
        "region",
        "name",
        "Mesh region name (default: single/default region)"
    );

    argList::addOption
    (
        "patch",
        "name",
        "Inlet patch name (overrides auto-detection)"
    );

    argList::addBoolOption
    (
        "listPatches",
        "List all available patches and exit"
    );

    #include "setRootCase.H"
    #include "createTime.H"

    // --- Region name (default region if -region is not given) ---
    const word regionName
    (
        args.getOrDefault<word>("region", fvMesh::defaultRegion)
    );

    const bool namedRegion = (regionName != fvMesh::defaultRegion);

    if (namedRegion)
    {
        Info<< "Using region: " << regionName << nl;
    }

    // Create the mesh manually (avoid createMesh.H which pulls in
    // simplifiedMeshes and extra dependencies).
    fvMesh mesh
    (
        IOobject
        (
            regionName,
            runTime.timeName(),
            runTime,
            IOobject::MUST_READ
        )
    );

    // --- List patches and exit if requested ---
    if (args.found("listPatches"))
    {
        Info<< "Available patches";
        if (namedRegion) Info<< " (region " << regionName << ")";
        Info<< ":" << nl;

        forAll(mesh.boundaryMesh(), patchi)
        {
            Info<< "  " << mesh.boundaryMesh()[patchi].name() << nl;
        }
        Info<< endl;
        return 0;
    }

    // --- Determine inlet patch name ---
    word patchName;

    if (args.found("patch"))
    {
        patchName = args.get<word>("patch");
    }
    else
    {
        // Auto-detect from common inlet names
        const wordList candidates({"inlet", "in"});
        bool found = false;
        for (const word& cand : candidates)
        {
            if (mesh.boundaryMesh().findPatchID(cand) >= 0)
            {
                patchName = cand;
                found = true;
                break;
            }
        }

        if (!found)
        {
            FatalErrorInFunction
                << "Could not auto-detect an inlet patch in region "
                << regionName << ". Available patches: "
                << mesh.boundaryMesh().names() << nl
                << "Use -patch <name> to specify one, or -listPatches to list."
                << nl
                << exit(FatalError);
        }

        Info<< "Auto-detected inlet patch: " << patchName << nl;
    }

    // --- Locate phiEq dictionary ---
    // Region case: constant/<region>/phiEq first, fall back to constant/phiEq
    fileName phiEqFile(runTime.constant()/"phiEq");

    if (namedRegion)
    {
        const fileName regionFile(runTime.constant()/regionName/"phiEq");

        if (isFile(regionFile))
        {
            phiEqFile = regionFile;
        }
    }

    Info<< "Reading " << phiEqFile << nl;

    IFstream is(phiEqFile);

    if (!is.good())
    {
        FatalErrorInFunction
            << "Cannot open " << phiEqFile << nl
            << "Create it with entry: value <phi>;" << nl
            << exit(FatalError);
    }

    const dictionary phiDict(is);

    const scalar phi = phiDict.get<scalar>("value");

    const scalar k_O_air =
        phiDict.getOrDefault<scalar>("kO2Air", 0.20946);
    const scalar K_fuel_oxidizer =
        phiDict.getOrDefault<scalar>("KfuelOxidizer", 0.5);
    const scalar Mass_CH4 =
        phiDict.getOrDefault<scalar>("MassCH4", 12.011 + 1.0080*4);
    const scalar Mass_O2 =
        phiDict.getOrDefault<scalar>("MassO2", 15.999*2);
    const scalar Mass_N2 =
        phiDict.getOrDefault<scalar>("MassN2", 14.007*2);

    // --- Mole fractions ---
    const scalar X_CH4 =
        K_fuel_oxidizer*k_O_air*phi
      / (K_fuel_oxidizer*k_O_air*phi + 1);
    const scalar X_O2 = (1 - X_CH4)*k_O_air;
    const scalar X_N2 = 1 - X_CH4 - X_O2;

    // --- Mixture molar mass ---
    const scalar Mass_mixture =
        Mass_CH4*X_CH4 + Mass_O2*X_O2 + Mass_N2*X_N2;

    // --- Mass fractions ---
    const scalar Y_CH4 = Mass_CH4/Mass_mixture*X_CH4;
    const scalar Y_O2  = Mass_O2 /Mass_mixture*X_O2;
    const scalar Y_N2  = Mass_N2 /Mass_mixture*X_N2;

    Info<< "Equivalence ratio phi = " << phi << nl
        << "Mole fractions:  X_CH4 = " << X_CH4
        << ", X_O2 = " << X_O2 << ", X_N2 = " << X_N2 << nl
        << "Mass fractions:  Y_CH4 = " << Y_CH4
        << ", Y_O2 = " << Y_O2 << ", Y_N2 = " << Y_N2 << nl
        << "Region: " << regionName << nl
        << "Inlet patch: " << patchName << nl
        << endl;

    // --- Update fields ---
    const wordList fieldNames({"CH4", "O2", "N2"});
    const scalarList fieldValues({Y_CH4, Y_O2, Y_N2});

    const label patchi = mesh.boundaryMesh().findPatchID(patchName);
    if (patchi < 0)
    {
        FatalErrorInFunction
            << "Patch " << patchName << " not found in region "
            << regionName << ". Available patches: "
            << mesh.boundaryMesh().names() << nl
            << exit(FatalError);
    }

    forAll(fieldNames, i)
    {
        const word& fname = fieldNames[i];
        const scalar Y = fieldValues[i];

        // mesh is the registry -> path automatically includes the region
        IOobject fieldHeader
        (
            fname,
            runTime.timeName(),
            mesh,
            IOobject::MUST_READ,
            IOobject::AUTO_WRITE
        );

        if (!fieldHeader.typeHeaderOk<volScalarField>(true))
        {
            FatalErrorInFunction
                << "Field " << fname << " not found in "
                << fieldHeader.objectPath() << nl
                << exit(FatalError);
        }

        volScalarField field(fieldHeader, mesh);

        // Set internal field
        field.primitiveFieldRef() = Y;

        // Set inlet patch
        fvPatchScalarField& pf = field.boundaryFieldRef()[patchi];
        pf == Y;

        // Ensure the patch is a fixedValue type
        if (pf.type() != "fixedValue")
        {
            WarningInFunction
                << "Patch " << patchName << " of field " << fname
                << " is of type " << pf.type()
                << ", not fixedValue. Setting its value anyway." << nl;
        }

        field.write();

        Info<< "Updated " << fieldHeader.objectPath() << ": internalField = "
            << Y << ", patch " << patchName << " = " << Y << nl;
    }

    Info<< nl << "Done." << nl << endl;

    return 0;
}


// ************************************************************************* //