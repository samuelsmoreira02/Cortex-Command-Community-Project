-- The persistent roster of veteran soldiers: who they are, what they've done, and the memorial of the fallen. Shared by the "Veterans" global script and its pie slice.

VeteranRoster = VeteranRoster or {};

VeteranRoster.FilePath = "UserSavedGames.rte/VeteranRoster.txt";
VeteranRoster.MaxActiveSoldiers = 60;
VeteranRoster.MaxMemorialEntries = 50;

VeteranRoster.XPPerKill = 25;
VeteranRoster.XPPerMission = 50;
VeteranRoster.XPPerVictory = 50;
VeteranRoster.MaxInjuries = 3;
VeteranRoster.HealthLostPerInjury = 0.1;

VeteranRoster.Ranks = {
	{Name = "Recruit", Short = "Rct", XP = 0},
	{Name = "Private", Short = "Pvt", XP = 75},
	{Name = "Corporal", Short = "Cpl", XP = 200},
	{Name = "Sergeant", Short = "Sgt", XP = 400},
	{Name = "Lieutenant", Short = "Lt", XP = 700},
	{Name = "Captain", Short = "Cpt", XP = 1100},
};

-- Every promotion past Private grants one of these, until a soldier has them all.
VeteranRoster.Traits = {
	Tough = "Tough: +25% health",
	EagleEye = "Eagle Eye: spots enemies from further away",
	FieldMedic = "Field Medic: slowly heals wounds",
	IronNerves = "Iron Nerves: shrugs off impacts",
};
VeteranRoster.TraitOrder = {"Tough", "EagleEye", "FieldMedic", "IronNerves"};

VeteranRoster.FirstNames = {"Ada", "Aiko", "Amara", "Anders", "Bruno", "Camila", "Chen", "Dario", "Dmitri", "Elena", "Emeka", "Farah", "Felix", "Greta", "Hana", "Ibrahim", "Ines", "Jonas", "Kofi", "Lars", "Leila", "Lucas", "Mateo", "Mei", "Nadia", "Nikolai", "Olga", "Omar", "Priya", "Rafael", "Rosa", "Samuel", "Sofia", "Tariq", "Thiago", "Uma", "Viktor", "Wanda", "Yusuf", "Zara"};
VeteranRoster.LastNames = {"Abara", "Bauer", "Castillo", "Dubois", "Eriksen", "Ferreira", "Gallo", "Haddad", "Ivanova", "Jensen", "Kowalski", "Lindqvist", "Moreira", "Nakamura", "Okafor", "Petrov", "Quinn", "Reyes", "Santos", "Tanaka", "Ueda", "Varga", "Weber", "Xavier", "Yilmaz", "Zielinski"};

function VeteranRoster.New()
	return {soldiers = {}, memorial = {}, nextID = 1};
end

function VeteranRoster.GetRankIndex(soldier)
	local rankIndex = 1;
	for index, rank in ipairs(VeteranRoster.Ranks) do
		if soldier.xp >= rank.XP then
			rankIndex = index;
		end
	end
	return rankIndex;
end

function VeteranRoster.GetRank(soldier)
	return VeteranRoster.Ranks[VeteranRoster.GetRankIndex(soldier)];
end

function VeteranRoster.GetTitle(soldier)
	return VeteranRoster.GetRank(soldier).Short .. ". " .. soldier.name;
end

-- "1 kill", "3 kills".
function VeteranRoster.Count(count, word)
	return count .. " " .. word .. (count == 1 and "" or "s");
end

function VeteranRoster.HasTrait(soldier, trait)
	for _, ownedTrait in ipairs(soldier.traits) do
		if ownedTrait == trait then
			return true;
		end
	end
	return false;
end

-- Picks a name nobody on the roster or the memorial has, so veterans can't be mistaken for each other.
function VeteranRoster.PickUniqueName(roster)
	local namesInUse = {};
	for _, soldier in ipairs(roster.soldiers) do
		namesInUse[soldier.name] = true;
	end
	for _, fallen in ipairs(roster.memorial) do
		namesInUse[fallen.name:match("^%a+%. (.+)$") or fallen.name] = true;
	end
	local name;
	for attempt = 1, 50 do
		name = VeteranRoster.FirstNames[math.random(#VeteranRoster.FirstNames)] .. " " .. VeteranRoster.LastNames[math.random(#VeteranRoster.LastNames)];
		if not namesInUse[name] then
			return name;
		end
	end
	-- Every name we tried was taken, so tell this one apart by their number.
	return name .. " " .. roster.nextID;
end

function VeteranRoster.CreateSoldier(roster, presetName, moduleName)
	local soldier = {
		id = roster.nextID,
		name = VeteranRoster.PickUniqueName(roster),
		presetName = presetName,
		moduleName = moduleName,
		xp = 0,
		kills = 0,
		missions = 0,
		injuries = 0,
		traits = {},
	};
	roster.nextID = roster.nextID + 1;
	table.insert(roster.soldiers, soldier);
	return soldier;
end

-- Adds experience, and returns the new rank and the trait it granted if the soldier got promoted.
function VeteranRoster.AddXP(soldier, xp)
	local oldRankIndex = VeteranRoster.GetRankIndex(soldier);
	soldier.xp = soldier.xp + xp;
	local newRankIndex = VeteranRoster.GetRankIndex(soldier);
	if newRankIndex <= oldRankIndex then
		return nil, nil;
	end
	local newTrait;
	for rankIndex = oldRankIndex + 1, newRankIndex do
		if rankIndex >= 3 then
			local availableTraits = {};
			for _, trait in ipairs(VeteranRoster.TraitOrder) do
				if not VeteranRoster.HasTrait(soldier, trait) then
					table.insert(availableTraits, trait);
				end
			end
			if #availableTraits > 0 then
				newTrait = availableTraits[math.random(#availableTraits)];
				table.insert(soldier.traits, newTrait);
			end
		end
	end
	return VeteranRoster.Ranks[newRankIndex], newTrait;
end

function VeteranRoster.MemorializeSoldier(roster, soldier, sceneName)
	for index, activeSoldier in ipairs(roster.soldiers) do
		if activeSoldier.id == soldier.id then
			table.remove(roster.soldiers, index);
			break;
		end
	end
	-- Recruits who fell before doing anything don't make it onto the memorial, or it'd fill up with them.
	if soldier.kills == 0 and soldier.missions == 0 then
		return;
	end
	table.insert(roster.memorial, 1, {name = VeteranRoster.GetTitle(soldier), kills = soldier.kills, missions = soldier.missions, sceneName = sceneName or "Unknown"});
	while #roster.memorial > VeteranRoster.MaxMemorialEntries do
		table.remove(roster.memorial);
	end
end

-- Soldiers are sorted best first, and only the best are kept if there are too many. Recruits who never finished a mission are never saved.
function VeteranRoster.SortSoldiers(roster)
	table.sort(roster.soldiers, function(a, b)
		if a.xp ~= b.xp then
			return a.xp > b.xp;
		end
		return a.id < b.id;
	end);
end

-- The roster is saved as lines of "|" separated fields, so a broken line only loses that one soldier.
function VeteranRoster.Load()
	local roster = VeteranRoster.New();
	if not LuaMan:FileExists(VeteranRoster.FilePath) then
		return roster;
	end
	local file = LuaMan:FileOpen(VeteranRoster.FilePath, "r");
	if file < 0 then
		return roster;
	end
	while not LuaMan:FileEOF(file) do
		local line = LuaMan:FileReadLine(file):gsub("[\r\n]", "");
		local fields = {};
		for field in (line .. "|"):gmatch("([^|]*)|") do
			table.insert(fields, field);
		end
		if fields[1] == "next" then
			roster.nextID = tonumber(fields[2]) or roster.nextID;
		elseif fields[1] == "soldier" and #fields >= 10 then
			local soldier = {
				id = tonumber(fields[2]) or 0,
				name = fields[3],
				presetName = fields[4],
				moduleName = fields[5],
				xp = tonumber(fields[6]) or 0,
				kills = tonumber(fields[7]) or 0,
				missions = tonumber(fields[8]) or 0,
				injuries = tonumber(fields[9]) or 0,
				traits = {},
			};
			for trait in fields[10]:gmatch("[^,]+") do
				if VeteranRoster.Traits[trait] then
					table.insert(soldier.traits, trait);
				end
			end
			table.insert(roster.soldiers, soldier);
			roster.nextID = math.max(roster.nextID, soldier.id + 1);
		elseif fields[1] == "fallen" and #fields >= 5 then
			table.insert(roster.memorial, {name = fields[2], kills = tonumber(fields[3]) or 0, missions = tonumber(fields[4]) or 0, sceneName = fields[5]});
		end
	end
	LuaMan:FileClose(file);
	VeteranRoster.SortSoldiers(roster);
	return roster;
end

function VeteranRoster.Save(roster)
	VeteranRoster.SortSoldiers(roster);
	local file = LuaMan:FileOpen(VeteranRoster.FilePath, "w");
	if file < 0 then
		return;
	end
	LuaMan:FileWriteLine(file, "next|" .. roster.nextID .. "\n");
	local saved = 0;
	for _, soldier in ipairs(roster.soldiers) do
		if saved < VeteranRoster.MaxActiveSoldiers and (soldier.missions > 0 or soldier.xp > 0) then
			saved = saved + 1;
			LuaMan:FileWriteLine(file, table.concat({"soldier", soldier.id, soldier.name, soldier.presetName, soldier.moduleName, soldier.xp, soldier.kills, soldier.missions, soldier.injuries, table.concat(soldier.traits, ",")}, "|") .. "\n");
		end
	end
	for _, fallen in ipairs(roster.memorial) do
		LuaMan:FileWriteLine(file, table.concat({"fallen", fallen.name, fallen.kills, fallen.missions, fallen.sceneName}, "|") .. "\n");
	end
	LuaMan:FileClose(file);
end
