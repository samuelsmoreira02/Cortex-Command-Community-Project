-- Pie slice functions for choosing an upgrade between Expedition sectors. The Expedition activity picks up the choice on its next update.

local function ChooseUpgrade(offerIndex)
	if ExpeditionRun and ExpeditionRun.offer and not ExpeditionRun.chosenOffer then
		ExpeditionRun.chosenOffer = offerIndex;
	end
end

function ExpeditionChooseUpgrade1(pieMenuOwner, pieMenu, pieSlice)
	ChooseUpgrade(1);
end

function ExpeditionChooseUpgrade2(pieMenuOwner, pieMenu, pieSlice)
	ChooseUpgrade(2);
end

function ExpeditionChooseUpgrade3(pieMenuOwner, pieMenu, pieSlice)
	ChooseUpgrade(3);
end
