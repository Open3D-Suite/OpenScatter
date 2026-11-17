-- SPDX-License-Identifier: GPL-3.0-or-later
-- Plugin test: do the instances reach the renderer, through the right route, once?
--
-- The renderer comes from the environment variable OSC_TEST_RENDERER (Arnold or Corona). The same
-- scatter - three models, each with its own material, one of them a Multi/Sub-Object and one with a
-- moved pivot - is rendered:
--   control       a plain box, to know the renderer renders at all
--   nothing       route "none": no pixel may be covered
--   reference     route "mesh": the aggregate mesh, what the instances must look like
--   the route     the route this renderer is given (instancing for Arnold, Particle Flow for Corona),
--                 set explicitly and then left on automatic: compared with the reference in colour
--   other routes  what this renderer does with the routes that are not its own
-- After each render the object's report tells which adapter handed instances out: exactly one may.
-- Result: results\render_<renderer>.txt, images in results\images\.

fileIn ((getFilenamePath (getSourceFileName())) + "common.ms")

(
    local rendererName = systemTools.getEnvVariable "OSC_TEST_RENDERER"
    if rendererName == undefined do rendererName = "Arnold"
    OSC_tOpen ("render_" + (toLower rendererName))
    try
    (
        local sc = OSC_tBuildScene()
        local n = sc.getInstanceCount()
        OSC_tWrite ("renderer: " + rendererName + "   instances: " + n as string + "   hash: " + sc.getInstanceHash())
        OSC_tWrite ("statistics: " + sc.getStatistics())
        if not (OSC_tUseRenderer rendererName) then OSC_tCheck ("the renderer " + rendererName + " is installed") false
        else
        (
            if rendererName == "Corona" do OSC_tWrite ("Corona " + (CoronaRenderer.CoronaFp.getVersionString()))
            local ownRoute = if rendererName == "Corona" then 2 else 1
            local ownName = if rendererName == "Corona" then "particleFlow" else "instancing"
            local ownField = if rendererName == "Corona" then "particleFlow" else "instancing"
            local tag = toLower rendererName
            -- pixels that may differ from the reference: anti-aliased edges, nothing more
            local tolerance = 160

            -- control
            sc.renderRoute = 4
            local shot = OSC_tShot saveAs:(tag + "_control")
            OSC_tCheck "control box: the renderer renders" (shot[2] > 500) detail:(OSC_tDescribe shot)
            OSC_tControl.isHidden = true

            -- nothing
            shot = OSC_tShot()
            OSC_tCheck "route none: nothing is rendered" (shot[2] == 0) detail:(OSC_tDescribe shot)
            OSC_tWrite ("        report: " + sc.getRenderReport())

            -- reference
            sc.renderRoute = 3
            local ref = OSC_tShot saveAs:(tag + "_reference_mesh")
            local report = sc.getRenderReport()
            OSC_tCheck "route mesh (reference): the aggregate mesh is rendered, in the models' colours" \
                (ref[2] > 2000 and ref[4] > 200 and ref[5] > 200 and ref[6] > 20 and ref[7] > 20) detail:(OSC_tDescribe ref)
            OSC_tCheck "route mesh: only the mesh adapter emitted, every instance" \
                ((OSC_tField report "mesh") == n and (OSC_tField report "instancing") == 0 and (OSC_tField report "particleFlow") == 0) detail:report
            -- how much two renders of the very same thing differ: the noise every comparison below carries
            OSC_tWrite ("  info  the reference rendered twice: different pixels=" + (OSC_tDiff ref (OSC_tShot())) as string)

            -- the renderer's own route, set explicitly
            sc.renderRoute = ownRoute
            shot = OSC_tShot saveAs:(tag + "_" + ownName)
            report = sc.getRenderReport()
            local diff = OSC_tDiff ref shot
            OSC_tCheck ("route " + ownName + ": matches the reference") (shot[2] > 2000 and diff <= tolerance) \
                detail:(OSC_tDescribe shot + " different from reference=" + diff as string)
            OSC_tCheck ("route " + ownName + ": only that adapter emitted, every instance") \
                ((OSC_tField report ownField) >= n and (OSC_tField report "mesh") == 0 and \
                 (OSC_tField report (if ownField == "instancing" then "particleFlow" else "instancing")) == 0) detail:report

            -- the same, chosen automatically
            sc.renderRoute = 0
            OSC_tCheck ("route auto picks " + ownName) (sc.getActiveRoute() == ownName) detail:(sc.getActiveRoute())
            shot = OSC_tShot saveAs:(tag + "_auto")
            report = sc.getRenderReport()
            diff = OSC_tDiff ref shot
            OSC_tCheck "route auto: matches the reference" (shot[2] > 2000 and diff <= tolerance) \
                detail:(OSC_tDescribe shot + " different from reference=" + diff as string)
            OSC_tCheck "route auto: only that adapter emitted, every instance" \
                ((OSC_tField report ownField) >= n and (OSC_tField report "mesh") == 0 and \
                 (OSC_tField report (if ownField == "instancing" then "particleFlow" else "instancing")) == 0) detail:report

            -- another frame: the instances do not depend on time. The reference is rendered at that
            -- frame too: a renderer changes its sampling pattern from frame to frame, which alone
            -- moves the anti-aliased edges (the second number below).
            shot = OSC_tShot frame:5 saveAs:(tag + "_auto_frame5")
            sc.renderRoute = 3
            local ref5 = OSC_tShot frame:5
            diff = OSC_tDiff ref5 shot
            OSC_tCheck "route auto at frame 5: matches the reference at frame 5" (shot[2] > 2000 and diff <= tolerance) \
                detail:(OSC_tDescribe shot + " different from reference=" + diff as string + \
                " (the reference itself, frame 0 against frame 5: " + (OSC_tDiff ref ref5) as string + ")")

            -- back to nothing: what the last render left behind must not be rendered again
            sc.renderRoute = 4
            shot = OSC_tShot()
            report = sc.getRenderReport()
            OSC_tCheck "route none after the other routes: nothing is rendered, nothing is left over" \
                (shot[2] == 0 and (OSC_tField report "particleFlow") == 0 and (OSC_tField report "instancing") == 0 and (OSC_tField report "mesh") == 0) \
                detail:(OSC_tDescribe shot + "   report: " + report)
            sc.renderRoute = 0

            -- a hidden scatter node renders nothing, whatever the route
            sc.isHidden = true
            shot = OSC_tShot()
            OSC_tCheck "a hidden scatter object renders nothing" (shot[2] == 0) detail:(OSC_tDescribe shot)
            sc.isHidden = false

            -- what this renderer does with the routes that are not its own (information, not a verdict)
            local otherRoute = if rendererName == "Corona" then 1 else 2
            local otherName = if rendererName == "Corona" then "instancing" else "particleFlow"
            sc.renderRoute = otherRoute
            sc.syncCarrier()
            shot = OSC_tShot saveAs:(tag + "_" + otherName)
            OSC_tWrite ("  info  route " + otherName + " in " + rendererName + ": " + OSC_tDescribe shot + \
                " different from reference=" + (OSC_tDiff ref shot) as string + "   report: " + sc.getRenderReport())

            -- more instances: time and memory tell real instances from copies. The model is shrunk so
            -- that the instances stand side by side instead of piling up on one another.
            sc.renderRoute = 0
            sc.models = #(OSC_tModels[1])
            sc.modelProbabilities = #(1.0)
            sc.scaleMin = [0.02, 0.02, 0.02]
            sc.scaleMax = [0.03, 0.03, 0.03]
            if rendererName == "Corona" do renderers.current.progressive_passLimit = 2   -- time the instances, not the passes
            local faces = (snapshotAsMesh OSC_tModels[1]).numFaces
            for d in #(2000.0, 20000.0, 40000.0) do
            (
                sc.density = d
                local count = sc.getInstanceCount()
                local before = sysinfo.getMAXMemoryInfo()
                shot = OSC_tShot()
                local after = sysinfo.getMAXMemoryInfo()
                OSC_tWrite ("  info  " + count as string + " instances of " + faces as string + " faces (" + \
                    (formattedPrint ((count as float) * faces / 1000000.0) format:".1f") + " M faces if copied): " + OSC_tDescribe shot + \
                    " peak memory +" + (((after[2] - before[2]) / 1048576.0) as integer) as string + " MB   scatter: " + \
                    (OSC_tField (sc.getStatistics()) "seconds") as string + " s   report: " + sc.getRenderReport())
                OSC_tCheck (count as string + " instances are rendered") (shot[2] > 2000 and (OSC_tField (sc.getRenderReport()) ownField) >= count) \
                    detail:(OSC_tDescribe shot)
            )
        )
    )
    catch (OSC_tCheck "no exception" false detail:(getCurrentException()))
    OSC_tClose()
)
