-- SPDX-License-Identifier: GPL-3.0-or-later
-- Plugin test: does OpenScatter load from its bundle, and does the object behave?
--   - the classes exist, an empty object is harmless, the parameters are scriptable;
--   - instances appear with a surface and models, and only an input that matters recomputes them;
--   - the Particle Flow carrier is created when Corona becomes the renderer, stays out of the way
--     and goes away with its scatter object;
--   - collapsing the object gives the aggregate mesh.
-- Result: results\load.txt

fileIn ((getFilenamePath (getSourceFileName())) + "common.ms")

(
	OSC_tOpen "load"
	try
	(
		OSC_tWrite ("3ds Max " + (maxVersion()) as string)
		OSC_tWrite ("ADSK_APPLICATION_PLUGINS = " + (systemTools.getEnvVariable "ADSK_APPLICATION_PLUGINS") as string)
		local scatterClass = execute "OpenScatter"
		local operatorClass = execute "OpenScatterOperator"
		OSC_tCheck "class OpenScatter is loaded" (scatterClass != undefined) detail:(scatterClass as string)
		OSC_tCheck "class OpenScatterOperator is loaded" (operatorClass != undefined) detail:(operatorClass as string)

		-- An object with nothing in it.
		resetMaxFile #noPrompt
		local empty = OpenScatter name:"OSC_t_empty"
		OSC_tWrite ("version: " + empty.getVersion())
		OSC_tWrite ("parameters: " + (getPropNames empty) as string)
		OSC_tCheck "an empty object has no instances" (empty.getInstanceCount() == 0) detail:(empty.getStatus())
		OSC_tCheck "an empty object says why" (empty.getStatus() == "no surface to scatter on")
		OSC_tCheck "defaults: density 1, route auto, viewport boxes" (empty.density == 1.0 and empty.renderRoute == 0 and empty.viewportMode == 1)
		local refused = false
		try (empty.surfaces = #(empty)) catch (refused = true)
		OSC_tCheck "the object refuses itself as a surface" (refused or empty.surfaces.count == 0)
		delete empty

		-- The test scene.
		local sc = OSC_tBuildScene()
		local n = sc.getInstanceCount()
		OSC_tWrite ("statistics: " + sc.getStatistics())
		OSC_tCheck "instances on the surface" (n > 100 and sc.getStatus() == "ok") detail:(n as string + " instances, status " + sc.getStatus())
		local perModel = for m = 1 to 3 collect (sc.getModelInstanceCount m)
		OSC_tCheck "every model is used, and the counts add up" (perModel[1] > 0 and perModel[2] > 0 and perModel[3] > 0 and \
			perModel[1] + perModel[2] + perModel[3] == n) detail:(perModel as string)
		OSC_tCheck "the first model is the most frequent (probability 2 : 1 : 1)" (perModel[1] > perModel[2] and perModel[1] > perModel[3])
		local tm = sc.getInstanceTransform 1
		OSC_tCheck "instance transforms are in the world, on the surface" ((abs tm.pos.x) <= 101 and (abs tm.pos.y) <= 101 and (abs tm.pos.z) <= 30) \
			detail:(tm.pos as string)

		-- Only what matters recomputes.
		local runs0 = OSC_tField (sc.getStatistics()) "scatterRuns"
		sc.viewportMode = 2
		sc.renderRoute = 3
		sc.meshMaxFaces = 1000000
		sc.getInstanceCount()
		local runs1 = OSC_tField (sc.getStatistics()) "scatterRuns"
		OSC_tCheck "display and route settings do not recompute the scatter" (runs1 == runs0) detail:(runs0 as string + " -> " + runs1 as string)
		move OSC_tModels[1] [100, 0, 0]
		sc.getInstanceCount()
		local runs2 = OSC_tField (sc.getStatistics()) "scatterRuns"
		OSC_tCheck "moving a model does not recompute the scatter" (runs2 == runs1) detail:(runs1 as string + " -> " + runs2 as string)
		local hash0 = sc.getInstanceHash()
		sc.seed = 8
		local hash1 = sc.getInstanceHash()
		local runs3 = OSC_tField (sc.getStatistics()) "scatterRuns"
		OSC_tCheck "a new seed recomputes, once, and gives another scatter" (runs3 == runs2 + 1 and hash1 != hash0) \
			detail:(hash0 + " -> " + hash1)
		sc.seed = 7
		OSC_tCheck "the old seed gives the old scatter back" (sc.getInstanceHash() == hash0)
		local z0 = (sc.getInstanceTransform 1).pos.z
		move OSC_tSurface [0, 0, 50]
		local z1 = (sc.getInstanceTransform 1).pos.z
		OSC_tCheck "moving the surface recomputes, and the instances follow it" ((abs (z1 - z0 - 50)) < 0.01) detail:(z0 as string + " -> " + z1 as string)
		move OSC_tSurface [0, 0, -50]

		-- Density: more instances, none of the old ones moved (the tile size stays inside one step).
		local tile0 = OSC_tField (sc.getStatistics()) "tileSize"
		local first = sc.getInstanceTransform 1
		sc.density = 20.0
		local n2 = sc.getInstanceCount()
		local tile1 = OSC_tField (sc.getStatistics()) "tileSize"
		OSC_tCheck "raising the density adds instances and keeps the tile size" (n2 > n and tile1 == tile0) \
			detail:(n as string + " -> " + n2 as string + ", tile " + tile0 as string + " -> " + tile1 as string)
		local found = false
		for i = 1 to n2 while not found do (if (distance (sc.getInstanceTransform i).pos first.pos) < 0.0001 do found = true)
		OSC_tCheck "an instance that was there is still there" found
		sc.density = 15.0
		OSC_tCheck "the old density gives the old scatter back" (sc.getInstanceHash() == hash0)

		-- The instance limit.
		sc.maxInstances = 50
		local limited = sc.getInstanceCount()
		OSC_tCheck "the instance limit holds" (limited <= 50 and limited > 0) detail:(limited as string)
		sc.maxInstances = 2000000

		-- Collapsing gives the aggregate mesh.
		local faces = 0
		for m = 1 to 3 do faces += (sc.getModelInstanceCount m) * (snapshotAsMesh OSC_tModels[m]).numFaces
		local snap = snapshotAsMesh sc
		OSC_tCheck "a snapshot is the aggregate mesh of every instance" (snap.numFaces == faces) detail:(snap.numFaces as string + " faces, expected " + faces as string)

		-- The combined material.
		sc.syncCarrier()
		local combined = sc.combinedMaterial
		OSC_tCheck "the combined material gathers the models' materials (1 + 1 + 2 slots)" \
			(classOf combined == Multimaterial and combined.numsubs == 4 and combined.materialList[1] == OSC_tModels[1].material and \
			 combined.materialList[3] == OSC_tModels[3].material.materialList[1]) detail:(combined as string)
		OSC_tCheck "the scatter node, which had no material, wears it" (sc.material == combined)

		-- A copy is a scatter of its own.
		local cp = copy sc
		cp.name = "OSC_t_copy"
		sc.syncCarrier()
		OSC_tCheck "a copy scatters the same and keeps a combined material of its own" \
			(cp.getInstanceHash() == sc.getInstanceHash() and cp.combinedMaterial != undefined and cp.combinedMaterial != sc.combinedMaterial and \
			 cp.material == cp.combinedMaterial and sc.material == sc.combinedMaterial) \
			detail:(cp.getInstanceCount() as string + " instances; materials " + cp.combinedMaterial as string + " / " + sc.combinedMaterial as string)
		delete cp

		-- The carrier.
		sc.renderRoute = 0
		OSC_tCheck "no carrier before Corona is the renderer" (sc.getCarrier() == undefined)
		if OSC_tUseRenderer "Corona" then
		(
			OSC_tCheck "with Corona the automatic route is Particle Flow" (sc.getActiveRoute() == "particleFlow") detail:(sc.getActiveRoute())
			local carrier = sc.getCarrier()
			OSC_tCheck "choosing Corona creates the carrier" (carrier != undefined) detail:(carrier as string)
			if carrier != undefined do
			(
				OSC_tCheck "the carrier is a PF Source, frozen, renderable, not hidden, with nothing to show in the viewport" \
					(classOf carrier == PF_Source and carrier.isFrozen and carrier.renderable and not carrier.isHidden and \
					 carrier.Quantity_Viewport == 0 and not carrier.Show_Logo and not carrier.Show_Emitter)
				local parts = for o in objects where (getUserProp o "OpenScatterCarrier") != undefined collect o
				OSC_tCheck "the carrier is four tagged nodes" (parts.count == 4) detail:((for o in parts collect o.name) as string)
				sc.syncCarrier()
				OSC_tCheck "a second sync does not create a second carrier" ((for o in objects where classOf o == PF_Source collect o).count == 1)

				-- Deleted by hand: what is left of it is cleared away and a whole one takes its place.
				delete carrier
				OSC_tCheck "without its PF Source the carrier does not count" (sc.getCarrier() == undefined)
				sc.syncCarrier()
				parts = for o in objects where (getUserProp o "OpenScatterCarrier") != undefined collect o
				OSC_tCheck "a carrier deleted by hand is built again, whole, and its leftovers are removed" \
					(sc.getCarrier() != undefined and parts.count == 4 and (for o in objects where classOf o == PF_Source collect o).count == 1) \
					detail:((for o in parts collect o.name) as string)
			)
			OSC_tUseRenderer "Arnold"
			OSC_tCheck "with Arnold the automatic route is the instancing interface" (sc.getActiveRoute() == "instancing") detail:(sc.getActiveRoute())
			OSC_tCheck "the carrier stays when the renderer changes" (sc.getCarrier() != undefined)
			OSC_tUseRenderer "Scanline"
			OSC_tCheck "with the scanline renderer the automatic route is the mesh" (sc.getActiveRoute() == "mesh") detail:(sc.getActiveRoute())
			OSC_tControl.isHidden = true
			local shot = OSC_tShot saveAs:"scanline_mesh"
			local report = sc.getRenderReport()
			OSC_tCheck "the scanline renderer renders the aggregate mesh, in the models' colours, and nothing else" \
				(shot[2] > 2000 and shot[4] > 200 and shot[5] > 200 and shot[6] > 20 and (OSC_tField report "mesh") == sc.getInstanceCount() and \
				 (OSC_tField report "particleFlow") == 0 and (OSC_tField report "instancing") == 0) detail:(OSC_tDescribe shot + "   report: " + report)

			-- A carrier goes away with its scatter object.
			local other = OpenScatter name:"OSC_t_other"
			delete sc
			other.syncCarrier()
			local left = for o in objects where (getUserProp o "OpenScatterCarrier") != undefined collect o.name
			OSC_tCheck "deleting the scatter object removes its carrier at the next sync" (left.count == 0) detail:(left as string)
		)
		else OSC_tWrite "  (Corona is not installed: the carrier checks were skipped)"
	)
	catch (OSC_tCheck "no exception" false detail:(getCurrentException()))
	OSC_tClose()
)
