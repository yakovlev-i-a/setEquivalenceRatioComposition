# setEquivalenceRatioComposition

A small OpenFOAM utility that sets the initial (internal) field and the inlet
patch value of the species mass fractions **CH4**, **O2** and **N2** from a
prescribed **equivalence ratio** $\phi$.

It is the "utility-only" alternative to the `fixedValueEquivalenceRatio`
boundary condition: instead of computing the composition at runtime on the
boundary, this utility writes the resulting mass fractions directly into the
`0/` field files as plain numbers.

## Why

For a premixed methane/air flame with a **fixed** equivalence ratio, the inlet
composition is constant. Writing the numbers directly into `0/CH4`, `0/O2` and
`0/N2` is simpler and more transparent than a runtime boundary condition, and
it keeps the case files readable by anyone.

## What it does

1. Reads `constant/phiEq`:

   ```
   value           1.0;    // equivalence ratio (required)
   ```

   Optional overrides (with defaults matching the `2S_CH4_BFER` mechanism):

   ```
   kO2Air          0.20946;   // O2 mole fraction in air
   KfuelOxidizer   0.5;       // moles O2 per mole CH4 (stoichiometry)
   MassCH4         16.0428;   // kg/kmol
   MassO2          31.9988;   // kg/kmol
   MassN2          28.0134;   // kg/kmol
   ```

2. Computes the mole fractions:

   $$X_{CH_4} = \frac{K_{fuel/ox}\,k_{O_2}^{air}\,\phi}
                    {K_{fuel/ox}\,k_{O_2}^{air}\,\phi + 1}$$

   $$X_{O_2} = (1 - X_{CH_4})\,k_{O_2}^{air}$$

   $$X_{N_2} = 1 - X_{CH_4} - X_{O_2}$$

3. Converts them to mass fractions via the mixture molar mass:

   $$Y_i = \frac{M_i\,X_i}{\sum_j M_j X_j}$$

4. Writes the result into `0/CH4`, `0/O2`, `0/N2`:
   - `internalField` → `uniform <Y_i>`
   - inlet patch (default `left`) → `fixedValue` with value `<Y_i>`

## Building

```bash
source /usr/lib/openfoam/openfoam2606/etc/bashrc
cd setEquivalenceRatioComposition
wmake
```

The executable is placed at `$FOAM_USER_APPBIN/setEquivalenceRatioComposition`.

## Usage

```bash
cd <your-case>

# auto-detect the inlet patch (tries: inlet, in)
setEquivalenceRatioComposition

# list all available patches
setEquivalenceRatioComposition -listPatches

# specify the inlet patch explicitly
setEquivalenceRatioComposition -patch inlet

# multi-region case: work on the region named "gas"
# (fields are read from / written to 0/gas/, mesh from constant/gas/polyMesh)
setEquivalenceRatioComposition -region gas

# multi-region case: list patches of the region "gas"
setEquivalenceRatioComposition -region gas -listPatches

# multi-region case: specify the inlet patch explicitly
setEquivalenceRatioComposition -region gas -patch gas_inlet
```

The utility reads `constant/phiEq` and updates `0/CH4`, `0/O2`, `0/N2`.

### Inlet patch selection

- By default, the inlet patch is **auto-detected** from the common names
  `inlet`, `in` (first match wins).
- Use `-patch <name>` to override the auto-detection.
- Use `-listPatches` to print all patch names and exit.
- If no candidate is found and no `-patch` is given, the utility exits with an
  error listing the available patches.

## Example

`constant/phiEq`:

```
value           0.8;
```

Run:

```bash
setEquivalenceRatioComposition
```

Output:

```
Equivalence ratio phi = 0.8
Mole fractions:  X_CH4 = ..., X_O2 = ..., X_N2 = ...
Mass fractions:  Y_CH4 = ..., Y_O2 = ..., Y_N2 = ...
Inlet patch: left

Updated CH4: internalField = ..., patch left = ...
Updated O2:  internalField = ..., patch left = ...
Updated N2:  internalField = ..., patch left = ...

Done.
```

## Notes

- The fields `0/CH4`, `0/O2`, `0/N2` must already exist (the utility reads and
  overwrites them). It does **not** create new fields.
- The inlet patch must exist in the mesh. Use `-patch <name>` if it is not
  called `left`.
- The inlet patch is set to `fixedValue`; if it was another type, a warning is
  printed but the value is still written.

## License

GPL v3 (based on OpenFOAM).
