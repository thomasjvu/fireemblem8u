"""Describe the scenario catalog embedded in the selected ROM build."""


def describe(catalog, choices):
    m, b, r, o = choices
    arena = catalog["maps"][m]
    objective = catalog["objectives"][o]
    return {
        "version": catalog["version"],
        "parties": [
            dict(
                seat=s,
                id=catalog["parties"][p]["id"],
                size=5,
                selection="curated_preset",
                units=catalog["parties"][p]["units"],
            )
            for s, p in enumerate([b, r])
        ],
        "party": {
            "size": 5,
            "level": 20,
            "selection": "curated_preset",
            "campaign_import": False,
            "experience_gain": False,
        },
        "map": {
            "id": arena["id"],
            "width": arena["width"],
            "height": arena["height"],
            "fog_of_war": False,
            "deployment": arena["deployment"],
            "castles": (
                [
                    {"seat": s, "x": xy[0], "y": xy[1]}
                    for s, xy in enumerate(arena["castles"])
                ]
                if o
                else []
            ),
        },
        "objective": {
            "id": objective,
            "seize_enabled": o != 0,
            "elimination_enabled": o != 1,
            "no_defenders_win": True,
            "round_limit": catalog["round_limit"],
            "round_limit_result": "draw",
            "mutual_elimination_result": "draw",
            "capture_rule": "Any living unspent friendly unit may move onto the enemy castle and spend its action to Seize.",
        },
        "outcomes": {
            "0": "in_progress",
            "1": "blue_win",
            "2": "red_win",
            "3": "draw",
            "4": "aborted",
        },
        "turns": "Alternate whole-army phases; one round includes both armies.",
    }
