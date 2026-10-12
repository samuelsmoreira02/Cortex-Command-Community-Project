-- Data and helpers for the Expedition activity: the run state that carries over between sectors, upgrades, unit records and permanent progress.
ExpeditionData = {};

ExpeditionData.SectorCount = 5;
ExpeditionData.Biomes = {"Expedition Highlands", "Expedition Desert", "Expedition Tundra"};
ExpeditionData.ProgressFilePath = "UserSavedGames.rte/ExpeditionProgress.txt";

-- Sectors cleared across all expeditions needed for each veterancy rank. Each rank gives more starting gold, and rank 3 and up an extra soldier.
ExpeditionData.SectorsPerVeterancyRank = 3;
ExpeditionData.MaxVeterancyRank = 5;
ExpeditionData.GoldPerVeterancyRank = 150;

ExpeditionData.Upgrades = {
	ReinforcedArmor = {Name = "Reinforced Armor", Description = "Your units take 25% less damage and can take more wounds."},
	FieldMedic = {Name = "Field Medic", Description = "Your units slowly heal over time."},
	Recruit = {Name = "Recruit", Description = "A new soldier joins your squad for the next sector."},
	WarChest = {Name = "War Chest", Description = "Gain 600 oz of gold for the next sector."},
	ExtraFuel = {Name = "Extra Fuel", Description = "Your units' jetpacks last 50% longer."},
	HeavyWeapons = {Name = "Heavy Weapons", Description = "Your commander gets a heavy weapon every sector."},
};
ExpeditionData.UpgradeOrder = {"ReinforcedArmor", "FieldMedic", "Recruit", "WarChest", "ExtraFuel", "HeavyWeapons"};

function ExpeditionData.IsExpeditionScene(sceneName)
	for _, biome in ipairs(ExpeditionData.Biomes) do
		if sceneName == biome then
			return true;
		end
	end
	return false;
end

-- Deterministic random numbers from the run's seed, so the same expedition seed always gives the same sectors.
-- math.random can't be used for this since it's shared with, and advanced by, everything else in the game.
function ExpeditionData.NextRandom(run)
	run.rng = (run.rng * 1103515245 + 12345) % 2147483648;
	return run.rng / 2147483648;
end

function ExpeditionData.NewRun(difficulty, firstSceneName)
	local progress = ExpeditionData.LoadProgress();
	local seed = math.floor(PosRand() * 2147483646) + 1;
	local run = {
		seed = seed,
		rng = seed,
		sector = 1,
		sectorCount = ExpeditionData.SectorCount,
		difficulty = difficulty,
		veterancy = ExpeditionData.GetVeterancyRank(progress),
		upgrades = {},
		squad = {},
		pendingFunds = 0,
		recruits = 0,
		advancing = false,
	};
	run.funds = 500 + run.veterancy * ExpeditionData.GoldPerVeterancyRank;
	-- The first sector is on whichever site the player picked, every next one is on a biome picked from the seed.
	run.sceneNames = {firstSceneName};
	for sector = 2, run.sectorCount do
		run.sceneNames[sector] = ExpeditionData.Biomes[math.floor(ExpeditionData.NextRandom(run) * #ExpeditionData.Biomes) + 1];
	end
	-- The first sector's terrain was already generated when the run starts, so record its seed rather than pick one.
	run.sectorSeeds = {SceneMan:GetLastProceduralTerrainSeed()};
	for sector = 2, run.sectorCount do
		run.sectorSeeds[sector] = math.floor(ExpeditionData.NextRandom(run) * 2147483646) + 1;
	end
	return run;
end

function ExpeditionData.GetSectorScene(run)
	return run.sceneNames[run.sector];
end

function ExpeditionData.GetSectorSeed(run)
	return run.sectorSeeds[run.sector];
end

function ExpeditionData.GetSectorObjective(run)
	if run.sector >= run.sectorCount then
		return "LastStand";
	end
	return run.sector % 2 == 1 and "Purge" or "Prospect";
end

function ExpeditionData.GetStartingSquadSize(run)
	return run.veterancy >= 3 and 3 or 2;
end

-- Upgrades

function ExpeditionData.GetUpgradeCount(run, upgradeName)
	return run and run.upgrades[upgradeName] or 0;
end

-- Picks three different upgrades to choose from.
function ExpeditionData.RollUpgradeOffer(run)
	local pool = {};
	for _, upgradeName in ipairs(ExpeditionData.UpgradeOrder) do
		table.insert(pool, upgradeName);
	end
	local offer = {};
	for i = 1, 3 do
		local index = math.floor(ExpeditionData.NextRandom(run) * #pool) + 1;
		table.insert(offer, table.remove(pool, index));
	end
	return offer;
end

function ExpeditionData.ApplyUpgrade(run, upgradeName)
	run.upgrades[upgradeName] = (run.upgrades[upgradeName] or 0) + 1;
	if upgradeName == "WarChest" then
		run.pendingFunds = run.pendingFunds + 600;
	elseif upgradeName == "Recruit" then
		run.recruits = run.recruits + 1;
	end
end

function ExpeditionData.TakePendingFunds(run)
	local funds = run.pendingFunds;
	run.pendingFunds = 0;
	return funds;
end

function ExpeditionData.AddRecruits(run, techName)
	for i = 1, run.recruits do
		table.insert(run.squad, ExpeditionData.NewSoldierRecord(techName));
	end
	run.recruits = 0;
end

-- Multiplies how much damage a unit and its limbs can take.
function ExpeditionData.ToughenUnit(unit, multiplier)
	local parts = {unit};
	if IsAHuman(unit) then
		unit = ToAHuman(unit);
		parts = {unit, unit.Head, unit.FGArm, unit.BGArm, unit.FGLeg, unit.BGLeg};
	end
	for _, part in pairs(parts) do
		if part then
			part.GibWoundLimit = math.ceil(part.GibWoundLimit * multiplier);
			part.DamageMultiplier = part.DamageMultiplier / multiplier;
		end
	end
end

function ExpeditionData.ApplyUpgradesToUnit(unit, run, techName, isCommander)
	local armor = ExpeditionData.GetUpgradeCount(run, "ReinforcedArmor");
	if armor > 0 then
		ExpeditionData.ToughenUnit(unit, 1.25 ^ armor);
	end
	local fuel = ExpeditionData.GetUpgradeCount(run, "ExtraFuel");
	if fuel > 0 and IsAHuman(unit) and ToAHuman(unit).Jetpack then
		local jetpack = ToAHuman(unit).Jetpack;
		jetpack.JetTimeTotal = jetpack.JetTimeTotal * (1.5 ^ fuel);
		jetpack.JetTimeLeft = jetpack.JetTimeTotal;
	end
	if isCommander and ExpeditionData.GetUpgradeCount(run, "HeavyWeapons") > 0 then
		local weapon = RandomHDFirearm("Weapons - Heavy", techName);
		if weapon then
			unit:AddInventoryItem(weapon);
		end
	end
end

-- Unit records: what's needed to bring a surviving unit and its gear into the next sector.

local function CreateEntityOfClass(className, presetName)
	local creators = {AHuman = CreateAHuman, ACrab = CreateACrab, HDFirearm = CreateHDFirearm, TDExplosive = CreateTDExplosive, HeldDevice = CreateHeldDevice, ThrownDevice = CreateThrownDevice};
	local creator = creators[className];
	return creator and creator(presetName) or nil;
end

function ExpeditionData.RecordUnit(actor)
	local record = {className = actor.ClassName, preset = actor:GetModuleAndPresetName(), items = {}};
	local function recordItem(item)
		if item and (item.ClassName == "HDFirearm" or item.ClassName == "TDExplosive" or item.ClassName == "HeldDevice" or item.ClassName == "ThrownDevice") then
			table.insert(record.items, {className = item.ClassName, preset = item:GetModuleAndPresetName()});
		end
	end
	if IsAHuman(actor) then
		recordItem(ToAHuman(actor).EquippedItem);
		recordItem(ToAHuman(actor).EquippedBGItem);
	end
	for item in actor.Inventory do
		recordItem(item);
	end
	return record;
end

function ExpeditionData.CreateUnit(record)
	local unit = CreateEntityOfClass(record.className, record.preset);
	if not unit then
		return nil;
	end
	for _, item in ipairs(record.items) do
		local newItem = CreateEntityOfClass(item.className, item.preset);
		if newItem then
			unit:AddInventoryItem(newItem);
		end
	end
	return unit;
end

local function RandomSoldier(techName, actorGroup)
	local soldier = RandomAHuman(actorGroup, techName) or RandomAHuman("Any", techName) or CreateAHuman("Soldier Light", "Coalition.rte");
	local items = {RandomHDFirearm("Weapons - Primary", techName), RandomHDFirearm("Weapons - Secondary", techName)};
	for _, item in pairs(items) do
		if item then
			soldier:AddInventoryItem(item);
		end
	end
	return soldier;
end

function ExpeditionData.NewSoldierRecord(techName)
	local soldier = RandomSoldier(techName, "Actors - Light");
	local record = ExpeditionData.RecordUnit(soldier);
	DeleteEntity(soldier);
	return record;
end

function ExpeditionData.NewCommanderRecord(techName)
	local commander = RandomSoldier(techName, "Actors - Heavy");
	local extras = {CreateHDFirearm("Medikit", "Base.rte"), RandomHDFirearm("Tools - Diggers", techName)};
	for _, item in pairs(extras) do
		if item then
			commander:AddInventoryItem(item);
		end
	end
	local record = ExpeditionData.RecordUnit(commander);
	DeleteEntity(commander);
	return record;
end

-- Returns a position on the ground at the given X, so the unit stands on the surface.
function ExpeditionData.GroundPos(x, unit)
	x = x % SceneMan.SceneWidth;
	return SceneMan:MovePointToGround(Vector(x, 0), unit.Radius, 3);
end

-- Permanent progress across expeditions, kept in a small text file of "key=value" lines.

function ExpeditionData.LoadProgress()
	local progress = {sectorsCleared = 0, expeditionsWon = 0, expeditionsLost = 0, deepestSector = 0};
	if not LuaMan:FileExists(ExpeditionData.ProgressFilePath) then
		return progress;
	end
	local file = LuaMan:FileOpen(ExpeditionData.ProgressFilePath, "r");
	if file < 0 then
		return progress;
	end
	while not LuaMan:FileEOF(file) do
		local key, value = string.match(LuaMan:FileReadLine(file), "^%s*(%w+)%s*=%s*(%d+)");
		if key and progress[key] then
			progress[key] = tonumber(value);
		end
	end
	LuaMan:FileClose(file);
	return progress;
end

function ExpeditionData.SaveProgress(progress)
	local file = LuaMan:FileOpen(ExpeditionData.ProgressFilePath, "w");
	if file < 0 then
		return;
	end
	for _, key in ipairs({"sectorsCleared", "expeditionsWon", "expeditionsLost", "deepestSector"}) do
		LuaMan:FileWriteLine(file, key .. "=" .. progress[key] .. "\n");
	end
	LuaMan:FileClose(file);
end

function ExpeditionData.GetVeterancyRank(progress)
	return math.min(math.floor(progress.sectorsCleared / ExpeditionData.SectorsPerVeterancyRank), ExpeditionData.MaxVeterancyRank);
end

function ExpeditionData.RecordSectorCleared(run)
	local progress = ExpeditionData.LoadProgress();
	progress.sectorsCleared = progress.sectorsCleared + 1;
	progress.deepestSector = math.max(progress.deepestSector, run.sector);
	ExpeditionData.SaveProgress(progress);
end

function ExpeditionData.RecordRunEnd(run, won)
	local progress = ExpeditionData.LoadProgress();
	if won then
		progress.expeditionsWon = progress.expeditionsWon + 1;
	else
		progress.expeditionsLost = progress.expeditionsLost + 1;
	end
	progress.deepestSector = math.max(progress.deepestSector, run.sector);
	ExpeditionData.SaveProgress(progress);
end

-- Serialization of the run state for saved games. Supports nested tables of strings, numbers and booleans.

function ExpeditionData.Serialize(value)
	local valueType = type(value);
	if valueType == "table" then
		local parts = {};
		for key, item in pairs(value) do
			table.insert(parts, "[" .. ExpeditionData.Serialize(key) .. "]=" .. ExpeditionData.Serialize(item));
		end
		return "{" .. table.concat(parts, ",") .. "}";
	elseif valueType == "string" then
		return string.format("%q", value);
	elseif valueType == "number" or valueType == "boolean" then
		return tostring(value);
	end
	return "nil";
end

function ExpeditionData.Deserialize(text)
	-- Parse with an empty environment so the saved text can only build data, not run anything.
	local chunk = loadstring("return " .. text);
	if not chunk then
		return nil;
	end
	setfenv(chunk, {});
	local ok, result = pcall(chunk);
	return ok and result or nil;
end
