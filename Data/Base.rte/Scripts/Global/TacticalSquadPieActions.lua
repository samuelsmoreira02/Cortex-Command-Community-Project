require("Scripts/Global/TacticalSquadOrders");

-- Pie slice functions for the "Squad Orders" sub pie menu. They're called with the squad leader as the pie menu owner.

function TacticalSquadHoldPosition(pieMenuOwner, pieMenu, pieSlice)
	TacticalSquadOrders.HoldPosition(ToActor(pieMenuOwner));
end

function TacticalSquadFollowLeader(pieMenuOwner, pieMenu, pieSlice)
	TacticalSquadOrders.FollowLeader(ToActor(pieMenuOwner));
end

function TacticalSquadToggleHoldFire(pieMenuOwner, pieMenu, pieSlice)
	TacticalSquadOrders.ToggleHoldFire(ToActor(pieMenuOwner));
end

function TacticalSquadCycleFormation(pieMenuOwner, pieMenu, pieSlice)
	TacticalSquadOrders.CycleFormation(ToActor(pieMenuOwner));
end
