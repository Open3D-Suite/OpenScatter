-- SPDX-License-Identifier: GPL-3.0-or-later
-- Plugin test: does a scene with an OpenScatter object survive being saved and reopened?
--
-- The runner starts this script once per step, each in its own process (OSC_TEST_STEP):
--   save      builds the scene, renders it in Corona (which creates the carrier) and saves it
--   reopen    opens it in a fresh process: same parameters, same instances, same picture, same carrier
--   missing   opens it WITHOUT the plugin (the runner hides the bundle): what is left, and saves again
--   restored  opens the scene saved without the plugin, with the plugin back
-- The steps share results\save_reopen.txt; facts one step leaves for the next go to a small file
-- in the work folder.

fileIn ((getFilenamePath (getSourceFileName())) + "common.ms")

(
    local step = systemTools.getEnvVariable "OSC_TEST_STEP"
    if step == undefined do step = "save"
    OSC_tOpen "save_reopen" append:(step != "save")
    local scene = OSC_tWork + "\\save_reopen.max"
    local sceneNoPlugin = OSC_tWork + "\\save_reopen_without_plugin.max"
    local factsFile = OSC_tWork + "\\save_reopen_facts.txt"
    OSC_tWrite ("=== step " + step + " (bundle folder: " + (systemTools.getEnvVariable "ADSK_APPLICATION_PLUGINS") as string + ")")
    try
    (
        local hasCorona = (execute "Corona") != undefined
        -- what the saved scene must still say after any round trip: count, hash, density, seed, covered pixels
        fn readFacts path =
        (
            local f = openFile path mode:"r"
            local parts = filterString (readLine f) " "
            close f
            parts
        )
        -- the checks a reopened scene has to pass
        fn checkAgainst facts hasCorona =
        (
            local sc = getNodeByName "OSC_t_scatter"
            OSC_tCheck "the object is an OpenScatter again" (sc != undefined and classOf sc.baseObject == OpenScatter) \
                detail:(if sc == undefined then "node not found" else (classOf sc.baseObject) as string)
            if sc != undefined and classOf sc.baseObject == OpenScatter do
            (
                OSC_tCheck "its parameters came back" (sc.surfaces.count == 1 and sc.models.count == 3 and sc.modelProbabilities[1] == 2.0 and \
                    (sc.density as string) == facts[3] and (sc.seed as string) == facts[4] and sc.alignToNormal == 0.5 and sc.scaleMax == [1.3, 1.3, 1.3]) \
                    detail:("density " + sc.density as string + ", seed " + sc.seed as string + ", surfaces " + sc.surfaces as string)
                OSC_tCheck "the same instances" ((sc.getInstanceCount() as string) == facts[1] and sc.getInstanceHash() == facts[2]) \
                    detail:(sc.getInstanceCount() as string + " instances, hash " + sc.getInstanceHash() + " (saved: " + facts[1] + ", " + facts[2] + ")")
                OSC_tCam = getNodeByName "OSC_t_cam"
                if hasCorona do
                (
                    OSC_tCheck "the carrier came back with the scene, once" (sc.getCarrier() != undefined and \
                        (for o in objects where classOf o == PF_Source collect o).count == 1) detail:(sc.getCarrier() as string)
                    OSC_tUseRenderer "Corona"
                    local shot = OSC_tShot saveAs:("save_reopen_" + (systemTools.getEnvVariable "OSC_TEST_STEP"))
                    local saved = facts[5] as integer
                    OSC_tCheck "Corona renders the same picture" ((abs (shot[2] - saved)) <= 40 and (OSC_tField (sc.getRenderReport()) "particleFlow") == (facts[1] as integer)) \
                        detail:(OSC_tDescribe shot + " (saved: covered=" + facts[5] + ")   report: " + sc.getRenderReport())
                )
                OSC_tUseRenderer "Arnold"
                local shot = OSC_tShot()
                OSC_tCheck "Arnold renders it through the instancing interface" (shot[2] > 2000 and (OSC_tField (sc.getRenderReport()) "instancing") >= (facts[1] as integer) and \
                    (OSC_tField (sc.getRenderReport()) "particleFlow") == 0) detail:(OSC_tDescribe shot + "   report: " + sc.getRenderReport())
            )
        )

        case step of
        (
            "save":
            (
                local sc = OSC_tBuildScene()
                OSC_tControl.isHidden = true
                local covered = 0
                if hasCorona then
                (
                    OSC_tUseRenderer "Corona"
                    local shot = OSC_tShot saveAs:"save_reopen_save"
                    covered = shot[2]
                    OSC_tCheck "Corona renders the scatter before saving" (covered > 2000 and sc.getCarrier() != undefined) detail:(OSC_tDescribe shot)
                )
                else OSC_tWrite "  (Corona is not installed: saved without a carrier)"
                local f = createFile factsFile
                format "% % % % %\n" (sc.getInstanceCount()) (sc.getInstanceHash()) sc.density sc.seed covered to:f
                close f
                OSC_tCheck "the scene is saved" (saveMaxFile scene quiet:true) detail:scene
            )
            "reopen":
            (
                local ok = loadMaxFile scene quiet:true missingExtFilesAction:#logmsg missingDLLsAction:#logmsg
                OSC_tCheck "the scene opens" ok
                checkAgainst (readFacts factsFile) hasCorona
            )
            "missing":
            (
                OSC_tCheck "the plugin is not loaded in this process" ((execute "OpenScatter") == undefined)
                local ok = loadMaxFile scene quiet:true missingExtFilesAction:#logmsg missingDLLsAction:#logmsg
                OSC_tCheck "the scene opens without the plugin" ok
                local sc = getNodeByName "OSC_t_scatter"
                OSC_tCheck "the object is kept as a stand-in" (sc != undefined and (classOf sc.baseObject) as string == "Missing_GeomObject") \
                    detail:(if sc == undefined then "node not found" else (classOf sc.baseObject) as string)
                OSC_tCam = getNodeByName "OSC_t_cam"
                OSC_tUseRenderer "Scanline"
                local shot = OSC_tShot()
                OSC_tCheck "nothing of it is rendered" (shot[2] == 0) detail:(OSC_tDescribe shot)
                OSC_tCheck "the scene is saved again, still without the plugin" (saveMaxFile sceneNoPlugin quiet:true)
            )
            "restored":
            (
                local ok = loadMaxFile sceneNoPlugin quiet:true missingExtFilesAction:#logmsg missingDLLsAction:#logmsg
                OSC_tCheck "the scene saved without the plugin opens with it" ok
                checkAgainst (readFacts factsFile) hasCorona
            )
        )
    )
    catch (OSC_tCheck "no exception" false detail:(getCurrentException()))
    OSC_tWrite ("STEP " + step + ": " + (if OSC_tFailed == 0 then "PASS" else "FAIL") + " (" + OSC_tChecks as string + " checks, " + OSC_tFailed as string + " failed)")
    close OSC_tLog
)
