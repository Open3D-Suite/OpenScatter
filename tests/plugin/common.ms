-- SPDX-License-Identifier: GPL-3.0-or-later
-- What the OpenScatter plugin tests share: the result file, the checks, the test scene and the
-- render helpers. Every test runs in its own 3dsmaxbatch process (run_plugin_tests.ps1), with the
-- bundle loaded from a private folder, and writes one result file that ends in "RESULT: PASS" or
-- "RESULT: FAIL". The tests reset the scene: never run one in a 3ds Max with real work in it.

global OSC_tLog = undefined
global OSC_tChecks = 0
global OSC_tFailed = 0
global OSC_tResults = ""
global OSC_tWork = ""

-- the test scene
global OSC_tScatter = undefined
global OSC_tSurface = undefined
global OSC_tCam = undefined
global OSC_tControl = undefined
global OSC_tModels = #()

global OSC_tWrite
fn OSC_tWrite msg = (format "%\n" msg to:OSC_tLog; flush OSC_tLog)

-- Opens <results>\<name>.txt. "append" keeps what an earlier step of the same test wrote.
global OSC_tOpen
fn OSC_tOpen name append:false =
(
    OSC_tResults = systemTools.getEnvVariable "OSC_TEST_RESULTS"
    OSC_tWork = systemTools.getEnvVariable "OSC_TEST_WORK"
    if OSC_tResults == undefined do OSC_tResults = (getFilenamePath (getSourceFileName())) + "results"
    if OSC_tWork == undefined do OSC_tWork = sysInfo.tempdir
    makeDir OSC_tResults all:true
    local path = OSC_tResults + "\\" + name + ".txt"
    OSC_tLog = if append and (doesFileExist path) then (openFile path mode:"a") else (createFile path)
    OSC_tChecks = 0
    OSC_tFailed = 0
)

global OSC_tCheck
fn OSC_tCheck what ok detail:"" =
(
    OSC_tChecks += 1
    if ok != true do OSC_tFailed += 1
    OSC_tWrite ((if ok == true then "  ok    " else "  FAIL  ") + what + (if detail != "" then (": " + detail) else ""))
    ok == true
)

global OSC_tClose
fn OSC_tClose =
(
    OSC_tWrite ("RESULT: " + (if OSC_tFailed == 0 then "PASS" else "FAIL") + " (" + OSC_tChecks as string + " checks, " + \
        OSC_tFailed as string + " failed)")
    close OSC_tLog
)
-- A material that only emits: no base colour and no reflection, so that nothing bounces light onto
-- its neighbours and two renders of the same scene differ only at anti-aliased edges.
global OSC_tEmissive
fn OSC_tEmissive nm col =
(
    local m = PhysicalMaterial name:nm
    m.emission = 1.0
    m.emit_color = col
    m.base_color = color 0 0 0
    m.base_weight = 0.0
    m.reflectivity = 0.0
    m
)

-- The scene every test starts from: a bumpy terrain that is not rendered itself, three models with
-- their own materials parked far outside the view, a control box, a camera and the scatter object.
--   model 1: a teapot, red.
--   model 2: a box whose pivot was moved, green: tells whether an output places the pivot or the origin.
--   model 3: a cylinder with a Multi/Sub-Object material (blue and yellow): its slots must survive.
global OSC_tBuildScene
fn OSC_tBuildScene density:15.0 =
(
    resetMaxFile #noPrompt
    backgroundColor = color 0 0 0
    local surf = Plane length:200 width:200 lengthsegs:24 widthsegs:24 pos:[0, 0, 0] name:"OSC_t_surface"
    addModifier surf (Noisemodifier seed:3 scale:60 strength:[0, 0, 25])
    convertToMesh surf
    surf.renderable = false

    local mRed = OSC_tEmissive "OSC_t_red" (color 255 0 0)
    local mGreen = OSC_tEmissive "OSC_t_green" (color 0 255 0)
    local mBlue = OSC_tEmissive "OSC_t_blue" (color 0 0 255)
    local mYellow = OSC_tEmissive "OSC_t_yellow" (color 255 255 0)
    local mTwo = Multimaterial numsubs:2 name:"OSC_t_two"
    mTwo.materialList[1] = mBlue
    mTwo.materialList[2] = mYellow

    local a = Teapot radius:4 segs:4 pos:[5000, 0, 0] name:"OSC_t_modelA"
    a.material = mRed
    local b = Box length:5 width:5 height:9 pos:[5200, 0, 0] name:"OSC_t_modelB"
    b.material = mGreen
    b.pivot = b.pos + [0, 0, 4.5]
    local c = Cylinder radius:3 height:8 sides:10 heightsegs:1 pos:[5400, 0, 0] name:"OSC_t_modelC"
    c.material = mTwo
    OSC_tModels = #(a, b, c)

    OSC_tControl = Box length:20 width:20 height:20 pos:[-70, -70, 30] name:"OSC_t_control"
    OSC_tControl.material = OSC_tEmissive "OSC_t_white" (color 255 255 255)
    OSC_tCam = Targetcamera pos:[0, -330, 260] target:(Targetobject pos:[0, 0, 0] name:"OSC_t_cam_target") name:"OSC_t_cam"

    local sc = OpenScatter name:"OSC_t_scatter"
    sc.surfaces = #(surf)
    sc.models = #(a, b, c)
    sc.modelProbabilities = #(2.0, 1.0, 1.0)
    sc.density = density
    sc.seed = 7
    sc.scaleMin = [0.7, 0.7, 0.7]
    sc.scaleMax = [1.3, 1.3, 1.3]
    sc.rotationMin = [-10, -10, 0]
    sc.rotationMax = [10, 10, 360]
    sc.alignToNormal = 0.5
    OSC_tSurface = surf
    OSC_tScatter = sc
    sc
)

-- Makes a renderer current and sets what a test render needs. Returns false when it is not installed.
global OSC_tUseRenderer
fn OSC_tUseRenderer name =
(
    local ok = true
    try
    (
        case name of
        (
            "Arnold":
            (
                renderers.current = Arnold()
                -- Without a batch licence Arnold returns an empty image unless told to carry on.
                renderers.current.abort_on_license_fail = false
                renderers.current.skip_license_check = true
            )
            "Corona":
            (
                renderers.current = Corona()
                -- enough passes for the anti-aliased edges to settle: two renders of the same thing
                -- then differ in a handful of pixels, not in hundreds
                renderers.current.progressive_passLimit = 32
            )
            "Scanline": renderers.current = Default_Scanline_Renderer()
            default: ok = false
        )
    )
    catch (ok = false)
    ok
)

-- Renders through the test camera. Returns #(pixels, covered pixels, seconds, red, green, blue, yellow).
global OSC_tShot
fn OSC_tShot frame:0 saveAs:"" =
(
    local t0 = timeStamp()
    local bm = render camera:OSC_tCam frame:frame outputSize:[320, 240] vfb:false
    local secs = (timeStamp() - t0) / 1000.0
    if saveAs != "" do
    (
        makeDir (OSC_tResults + "\\images") all:true
        bm.filename = OSC_tResults + "\\images\\" + saveAs + ".png"
        save bm
    )
    local px = #()
    local covered = 0
    local nr = 0
    local ng = 0
    local nb = 0
    local ny = 0
    for y = 0 to (bm.height - 1) do
    (
        local row = getPixels bm [0, y] bm.width
        for c in row do
        (
            append px c
            if c.alpha > 8 do
            (
                covered += 1
                if c.r > 128 and c.g < 96 and c.b < 96 do nr += 1
                if c.g > 128 and c.r < 96 and c.b < 96 do ng += 1
                if c.b > 128 and c.r < 96 and c.g < 96 do nb += 1
                if c.r > 128 and c.g > 128 and c.b < 96 do ny += 1
            )
        )
    )
    close bm
    #(px, covered, secs, nr, ng, nb, ny)
)

-- Pixels that differ in colour or coverage between two shots.
global OSC_tDiff
fn OSC_tDiff a b =
(
    local n = 0
    local pa = a[1]
    local pb = b[1]
    for i = 1 to pa.count do
    (
        local p = pa[i]
        local q = pb[i]
        if (abs (p.r - q.r)) > 48 or (abs (p.g - q.g)) > 48 or (abs (p.b - q.b)) > 48 or (abs (p.alpha - q.alpha)) > 64 do n += 1
    )
    n
)

global OSC_tDescribe
fn OSC_tDescribe shot =
(
    "covered=" + shot[2] as string + " red=" + shot[4] as string + " green=" + shot[5] as string + " blue=" + shot[6] as string + \
        " yellow=" + shot[7] as string + " seconds=" + shot[3] as string
)

-- One number out of a "name=value name=value" report string (0 when it is not there).
global OSC_tField
fn OSC_tField report name =
(
    local value = 0
    for part in (filterString report " ") do
    (
        local kv = filterString part "="
        if kv.count == 2 and kv[1] == name do value = (kv[2] as number)
    )
    if value == undefined then 0 else value
)
