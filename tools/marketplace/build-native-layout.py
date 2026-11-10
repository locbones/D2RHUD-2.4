"""Author the native RMD stash footer button and marketplace widget tree.

Output JSON is ordinary editable D2R layout data, not an overlay description.
"""
import json
from pathlib import Path

ROOT = Path(__file__).parent / "layouts"
ART = "..\\..\\..\\ReMoDDeD\\Panels\\"
MARKET_ART = "..\\..\\..\\Pandemonium\\Marketplace\\"


def widget(kind, name, fields=None, children=None):
    value = {"type": kind, "name": name, "fields": fields or {}}
    if children is not None:
        value["children"] = children
    return value


def rect(x, y, w, h):
    return dict(x=x, y=y, width=w, height=h)


def label(name, text, x, y, w, h=62, center=False, color="$FontColorLightGold", size="$SmallPanelFontSize"):
    return widget("TextBoxWidget", name, {"rect": rect(x, y, w, h), "text": text,
                  "style": {"fontColor": color, "pointSize": size,
                      "alignment": {"h": "center" if center else "left", "v": "center" if center else "top"},
                      "options": {"lineWrap": True, "newlineHandling": "standard", "pipeCharacterBehavior": "handle"}}})


def item_link(name, x, y, w, h):
    # Native chat layouts use an embedded text renderer on this widget, hence
    # text/style rather than the ordinary TextBoxWidget's top-level style.
    # The native item-link binder supplies its text and serialized item data.
    return widget("TextBoxWithLinksWidget", name, {"rect": rect(x, y, w, h),
        "text/fontType": "Chat", "text/style": {
            "alignment": {"h": "left", "v": "top"},
            "options": {"pipeCharacterBehavior": "handle", "newlineHandling": "standard", "lineWrap": True},
            "fontColor": "$FontColorLightGold", "fontFace": "BlizzardGlobal", "pointSize": "$SmallPanelFontSize"}})


def dropdown(name, states, x, y, w=366):
    # Match RMD-MP's BankPages DropdownListWidget fields. Selection/caption are
    # synchronized through the game's setter; no cycling-button action is used.
    return widget("DropdownListWidget", name, {"rect": rect(x, y, w, 68),
        "background/rect": rect(20, 0, w, 68),
        "background/leftCapOffset": "$OptionsDropDownLeftCapOffset2",
        "background/rightCapOffset": "$OptionsDropDownRightCapOffset2",
        "pressedFrame": 1, "disabledFrame": 2, "hoveredFrame": 3,
        "focusIndicatorFilename": "$OptionsDropDownFocusIndicatorFilename",
        "states": states, "text/style": "$StyleSettingsCarousel", "textColor": "$FontColorLightGold"})


def button(name, caption, command, x, y, small=False):
    w, h = (366, 68) if small else (472, 87)
    return widget("ButtonWidget", name, {"rect": rect(x, y, w, h),
        "filename": ART + "Buttons\\" + ("ButtonSmall_R" if small else "ButtonMed_R"),
        "pressedFrame": 1, "disabledFrame": 2, "hoveredFrame": 3,
        "onClickMessage": "BankPanelMessage:" + command},
        [label(name + "Label", caption, 12, 0, w - 24, h, True)])


def input_box(name, hint, x, y, w, maximum=15):
    # An InputTextBox's own background is barely visible in the native renderer.
    # Explicit native rectangles give every field a persistent, readable edge.
    return widget("Widget", name + "Field", {"rect": rect(x, y, w, 66)}, [
        fill(name + "Border", 0, 0, w, 66, [0.65, 0.48, 0.25, 1]),
        fill(name + "Recess", 3, 3, w - 6, 60, [0.055, 0.045, 0.032, 1]),
        fill(name + "Underline", 10, 61, w - 20, 2, [0.9, 0.71, 0.39, 1]),
        widget("InputTextBoxWidget", name, {"rect": rect(6, 5, w - 12, 56),
        "fontType": "Chat", "maxStringLength": maximum,
        "backgroundColor": [0, 0, 0, 0],
        "text/rect": rect(10, 0, w - 32, 56), "tooltipString": hint,
        "fontStyle": {"fontColor": "$FontColorLightGold", "pointSize": "$MediumFontSize",
                      "fontFace": "BlizzardGlobal", "alignment": {"h": "left", "v": "center"}}})])


def group(name, children, visible=True):
    return widget("Widget", name, {"rect": rect(0, 0, 2250, 1635), "visible": visible}, children)


# Reuse installed RMD art. ImageWidget takes its natural sprite dimensions;
# rect/scale preserves its aspect instead of assuming rect width stretches it.
def art(name, filename, x, y, scale=1):
    return widget("ImageWidget", name, {"rect": {"x": x, "y": y, "scale": scale}, "filename": filename})


def fill(name, x, y, w, h, color):
    return widget("RectangleWidget", name, {"rect": rect(x, y, w, h), "color": color})


def frame(name, x, y, w, h, selected=False):
    edge = [0.72, 0.53, 0.26, 1] if selected else [0.28, 0.22, 0.14, 0.9]
    return [fill(name + "Edge", x, y, w, h, edge),
            fill(name + "Inset", x + 2, y + 2, w - 4, h - 4, [0.015, 0.014, 0.013, 0.94])]


def divider(name, x, y, w):
    return art(name, ART + "Menus\\Divider", x, y, w / 2130)


def focused_views(filters):
    # One page at a time: collections never compete with an open item/thread.
    collection = frame("MarketListingsPanel", 58, 470, 2112, 1020) + [
        group("MarketFilters", filters),
        group("MarketActivityFilter", [dropdown("MarketActivityType", ["Conversations", "Saved drafts"], 80, 350, 600)]),
        label("MarketListTitle", "PLAYER OFFERS", 90, 492, 1400, 40, color="$FontColorGoldYellow", size=26),
        label("MarketCount", "", 1560, 495, 580, 38, size=22),
        divider("MarketListingsDivider", 80, 548, 2060),
        group("MarketEmptyArt", [art("MarketEmptyIcon", ART + "Shop\\Shop_Button", 1060, 830)]),
        label("MarketEmpty", "No matching items.", 430, 1020, 1390, 140, True, color="$FontColorBeige"),
        button("MarketPrevButton", "Previous", "MarketPrev", 80, 1390, True),
        button("MarketNextButton", "Next", "MarketNext", 1774, 1390, True)]
    for n in range(6):
        x, y = 80 + n % 2 * 1060, 590 + n // 2 * 262
        collection.append(widget("ButtonWidget", f"MarketCard{n}", {"rect": rect(x, y, 1000, 242), "filename": "Blank",
            "onClickMessage": f"BankPanelMessage:MarketCard{n}"},
            frame(f"MarketCard{n}Frame", 0, 0, 1000, 242) + [
                widget("Widget", f"MarketCardSelected{n}", {"rect": rect(0, 0, 1000, 242), "visible": False},
                    frame(f"MarketCard{n}Selection", 0, 0, 1000, 242, True)),
                *frame(f"MarketCard{n}Well", 18, 15, 154, 212),
                widget("ButtonWidget", f"MarketInspect{n}", {"rect": rect(30, 26, 128, 190), "filename": "Blank",
                    "onClickMessage": f"BankPanelMessage:MarketInspect{n}"}, [
                        widget("ImageWidget", f"MarketCardImage{n}", {"rect": rect(0, 0, 128, 190), "filename": "Blank"})]),
                item_link(f"MarketCardName{n}", 200, 20, 770, 66),
                label(f"MarketCardTag{n}", "ITEM", 200, 202, 770, 28, color="$FontColorGoldYellow", size=18),
                label(f"MarketCardInfo{n}", "", 200, 100, 770, 86, color="$FontColorBeige", size=24)]))

    hero = [widget("ButtonWidget", "MarketInspectSelected", {"rect": rect(80, 452, 150, 160), "filename": "Blank",
                "onClickMessage": "BankPanelMessage:MarketInspectSelected"}, frame("MarketSelectedWell", 0, 0, 150, 160) + [
                    widget("ImageWidget", "MarketItemImage", {"rect": rect(2, 2, 146, 156), "filename": "Blank"})]),
            item_link("MarketItemSummary", 270, 462, 1410, 64),
            label("MarketItemFacts", "", 270, 538, 1410, 44, size=24),
            label("MarketItemExtra", "Click the item to inspect", 270, 588, 1410, 44, color="$FontColorBeige", size=20)]
    term_rows = []
    for n in range(4):
        y = 760 + n * 112
        term_rows.append(group(f"MarketTermRow{n}", frame(f"MarketTermFrame{n}", 80, y, 2060, 96) + [
            widget("ImageWidget", f"MarketTermImage{n}", {"rect": rect(106, y + 12, 70, 70), "filename": "Blank"}),
            label(f"MarketTerm{n}", "", 210, y + 16, 1890, 68, size=26)]))
    # Retain old names for native snapshots; their parent is always hidden.
    term_rows.append(group("MarketUnusedTerms", [label(f"MarketTerm{n}", "", 100, 760, 1900, 60) for n in range(4,16)], False))
    details = [group("MarketDetailBody", [
        label("MarketPaymentBundle", "Asking price", 90, 666, 2000, 50, color="$FontColorGoldYellow", size=26),
        divider("MarketPaymentDivider", 80, 724, 2060)] + term_rows + [
        button("MarketTermsPrevButton", "Previous", "MarketTermsPrev", 80, 1230, True),
        button("MarketTermsNextButton", "Next", "MarketTermsNext", 480, 1230, True),
        label("MarketTermsPage", "", 900, 1245, 750, 40, True, color="$FontColorBeige", size=22),
        button("MarketFullPriceButton", "Full price", "MarketFullPrice", 1774, 1230, True)]),
        button("MarketPriceButton", "Set asking price", "MarketPrice", 80, 1385),
        button("MarketConfirmButton", "Offer asking price", "MarketConfirm", 80, 1385),
        button("MarketCounterButton", "Counter-offer", "MarketCounter", 610, 1385),
        button("MarketHistoryButton", "Item history", "MarketHistory", 1668, 1385)]
    details.append(button("MarketGiftItemButton", "Gift to player", "MarketGiftItem", 1140, 1385, True))
    details.append(button("MarketReturnButton", "Return to delivery", "MarketReturn", 610, 1385))
    details.append(group("MarketListingCommentDisplay", [
        label("MarketListingCommentText", "", 80, 1314, 2060, 60, color="$FontColorBeige", size=22)], False))
    details[1]["children"][0]["name"] = "MarketPriceLabel"
    details[2]["children"][0]["name"] = "MarketConfirmLabel"
    price_lines = []
    for n in range(4):
        y = 900 + n * 108
        price_lines.append(group(f"MarketPriceLine{n}", frame(f"MarketPriceRow{n}", 80, y - 10, 2060, 100) + [
            widget("ImageWidget", f"MarketPriceImage{n}", {"rect": rect(110, y + 20, 40, 36), "filename": "Blank"}),
            label(f"MarketPriceName{n}", "Payment", 180, y + 20, 780, 60, size=26),
            label(f"MarketQuantityTitle{n}", "Amount", 1000, y + 20, 150, 40, size=22),
            input_box(f"Quantity{n}", "Enter the amount", 1160, y + 10, 260),
            button(f"MarketEth{n}", "Any condition", f"MarketEth{n}", 1470, y + 10, True),
            dropdown(f"MarketTorchClass{n}", ["Any class", "Amazon", "Sorceress", "Necromancer", "Paladin", "Barbarian", "Druid", "Assassin"], 1470, y + 10),
            label(f"MarketUnits{n}", "Stored units", 1470, y + 20, 366, 40, True, color="$FontColorBeige", size=22),
            widget("ButtonWidget", f"MarketRemove{n}", {"rect": rect(1870, y + 10, 240, 68), "filename": "Blank",
                "onClickMessage": f"BankPanelMessage:MarketRemove{n}"}, frame(f"MarketRemoveFrame{n}", 0, 0, 240, 68) +
                [label(f"MarketRemoveLabel{n}", "Remove", 12, 0, 216, 68, True)])]))
        next(c for c in price_lines[-1]["children"] if c["name"] == f"MarketEth{n}")["children"][0]["name"] = f"MarketEthLabel{n}"
        # Older HUDs can receive updated layouts before their DLL is upgraded.
        next(c for c in price_lines[-1]["children"] if c["name"] == f"MarketTorchClass{n}")["fields"]["visible"] = False
    price_lines += [button("MarketPricePrevButton", "Previous", "MarketPricePrev", 80, 1350, True),
                    button("MarketPriceNextButton", "Next", "MarketPriceNext", 1774, 1350, True)]
    picker = [label("MarketPaymentTypeTitle", "PAY WITH", 80, 770, 430, 34, color="$FontColorGoldYellow", size=22),
        label("MarketPaymentSearchTitle", "FIND AN ITEM", 580, 770, 1560, 34, color="$FontColorGoldYellow", size=22),
        dropdown("MarketPaymentKind", ["Items / runes", "Bag contents", "Gold"], 80, 815, 450),
        input_box("PaymentSearch", "Type an item name", 580, 815, 1560),
        label("MarketGoldHelp", "Gold stored in your deposit page", 580, 822, 1560, 56, color="$FontColorBeige", size=24),
        label("MarketPaymentCount", "", 80, 905, 2060, 35, color="$FontColorBeige", size=22)]
    for n in range(4):
        picker.append(widget("ButtonWidget", f"MarketPayment{n}", {"rect": rect(80, 960 + n * 105, 2060, 90), "filename": "Blank",
            "onClickMessage": f"BankPanelMessage:MarketPayment{n}"}, frame(f"MarketPaymentRow{n}", 0, 0, 2060, 90) + [
                widget("ImageWidget", f"MarketPaymentImage{n}", {"rect": rect(24, 12, 60, 66), "filename": "Blank"}),
                label(f"MarketPaymentLabel{n}", "", 120, 12, 1870, 66, size=26)]))
    picker += [button("MarketPaymentPrevButton", "Previous", "MarketPaymentPrev", 80, 1390, True),
               button("MarketPaymentNextButton", "Next", "MarketPaymentNext", 1774, 1390, True)]
    editor = [group("MarketPriceLines", price_lines), group("MarketPaymentPicker", picker),
        group("MarketListingCommentGroup", [label("MarketListingCommentLabel", "LISTING COMMENT (OPTIONAL)", 80, 758, 2060, 30, color="$FontColorGoldYellow", size=22),
            input_box("MarketListingComment", "A short note for players viewing this trade (240 characters)", 80, 798, 2060, 240)], False),
        group("MarketPriceStart", [art("MarketPriceStartIcon", "items/custom/GoldBar", 1030, 870, .75),
            label("MarketPriceStartTitle", "WHAT WOULD YOU LIKE IN RETURN?", 350, 1080, 1550, 54, True, color="$FontColorGoldYellow", size=30),
            label("MarketPriceStartHelp", "Add a payment, then choose the amount.", 350, 1160, 1550, 60, True, color="$FontColorBeige", size=26),
            label("MarketPriceStartExample", "Items, runes, bag contents or gold", 350, 1250, 1550, 48, True, color="$FontColorBeige", size=22)]),
        button("MarketAddPaymentButton", "Add payment", "MarketAddPayment", 80, 660),
        button("MarketFreePriceButton", "List for free", "MarketFreePrice", 610, 660),
        button("MarketSavePriceButton", "Save asking price", "MarketSavePrice", 610, 1430, True),
        button("MarketRemovePriceButton", "Unlist item", "MarketRemovePrice", 1010, 1430, True),
        button("MarketCancelPriceButton", "Cancel", "MarketCancelPrice", 1774, 660, True),
        label("MarketEditNotice", "", 470, 1528, 1660, 80, color="$FontColorBeige", size=22)]

    def log_row(name, y, height, conversation=False):
        children = frame(name + "Frame", 0, 0, 2060, height)
        if conversation:
            children += [fill(name + "Accent", 0, 0, 4, height, [.54, .36, .16, 1]),
                label(name + "Meta", "", 24, 10, 1360, 28, color="$FontColorGoldYellow", size=21),
                label(name + "Time", "", 1450, 10, 580, 28, size=18),
                label(name + "Text", "", 24, 46, 2006, height - 50, color="$FontColorBeige", size=24)]
        else:
            children += [label(name + "Text", "", 24, 16, 2012, height - 26, color="$FontColorBeige", size=24)]
        return widget("ButtonWidget", name, {"rect": rect(80, y, 2060, height), "filename": "Blank",
            "onClickMessage": "BankPanelMessage:" + name}, children)

    history = [dropdown("MarketHistoryKind", ["This item's history", "Similar sales"], 80, 660, 620),
        button("MarketBackHistoryButton", "Back to item", "MarketBackHistory", 1774, 660, True),
        label("MarketHistorySummary", "", 80, 750, 2060, 50, color="$FontColorBeige", size=24),
        button("MarketHistoryPrevButton", "Previous", "MarketHistoryPrev", 80, 1390, True),
        button("MarketHistoryNextButton", "Next", "MarketHistoryNext", 1774, 1390, True)]
    history.append(group("MarketHistoryExact", [log_row(f"MarketSale{n}", 835 + n * 170, 150) for n in range(3)]))
    chart = [dropdown("MarketChartCurrency", ["Gold", "Low rune points", "Mid rune points", "High rune points", "Ultra rune points", "Gems", "Zod Runes"], 80, 815, 450),
        label("MarketChartSummary", "", 580, 830, 1560, 45, color="$FontColorBeige", size=22),
        fill("MarketChartWell", 200, 910, 1840, 200, [.035, .03, .022, 1]),
        fill("MarketChartAxis", 200, 1108, 1840, 2, [.55, .4, .2, 1]),
        label("MarketChartMax", "0", 80, 930, 110, 50, size=22),
        label("MarketChartFirst", "", 200, 1120, 800, 30, size=20),
        label("MarketChartLast", "", 1240, 1120, 800, 30, True, size=20)]
    for n in range(16):
        chart.append(fill(f"MarketChartBar{n}", 210 + n * 100, 1000, 16, 100, [.73, .51, .23, 1]))
    history.append(group("MarketHistorySimilar", chart + [log_row(f"MarketSimilarSale{n}", 1170 + n * 98, 88) for n in range(2)]))
    history.append(label("MarketHistoryEmpty", "No completed sales yet.", 430, 1040, 1390, 80, True, color="$FontColorBeige", size=28))

    thread = frame("MarketCurrentOffer", 80, 650, 2060, 130) + [
        widget("ImageWidget", "MarketThreadPaymentImage", {"rect": rect(105, 680, 74, 70), "filename": "Blank"}),
        label("MarketThreadState", "", 210, 663, 1480, 35, color="$FontColorGoldYellow", size=22),
        label("MarketThreadTerms", "", 210, 714, 1480, 48, size=28),
        button("MarketReviewOfferButton", "View all terms", "MarketReviewOffer", 1774, 685, True),
        button("MarketAcceptOfferButton", "Accept offer", "MarketAcceptOffer", 80, 805, True),
        button("MarketRejectOfferButton", "Decline", "MarketRejectOffer", 480, 805, True),
        button("MarketCounterOfferButton", "Counter-offer", "MarketCounterOffer", 880, 805, True),
        button("MarketWithdrawOfferButton", "Withdraw", "MarketWithdrawOffer", 1280, 805, True),
        button("MarketThreadHistoryButton", "Item history", "MarketThreadHistory", 1774, 805, True),
        label("MarketThreadLogTitle", "CONVERSATION", 80, 905, 1350, 32, color="$FontColorGoldYellow", size=24),
        label("MarketLogHint", "Click an entry to read more", 1530, 910, 610, 28, color="$FontColorBeige", size=20)]
    thread += [log_row(f"MarketLog{n}", 955 + n * 110, 100, True) for n in range(3)]
    thread += [label("MarketMessageTitle", "YOUR MESSAGE", 80, 1370, 2060, 32, color="$FontColorGoldYellow", size=22),
        input_box("MarketOfferMessage", "Write a message", 80, 1410, 1630, 512),
        button("MarketSendMessageButton", "Send message", "MarketSendMessage", 1774, 1410, True),
        button("MarketLogPrevButton", "Older", "MarketLogPrev", 80, 1290, True),
        button("MarketLogNextButton", "Newer", "MarketLogNext", 1774, 1290, True),
        label("MarketLogPage", "", 650, 1308, 950, 40, True, color="$FontColorBeige", size=22),
        # Retained as hidden compatibility controls for queued native choices.
        group("MarketLegacyDecisions", [dropdown("MarketOfferAction", ["Accept", "Decline", "Withdraw", "Counter-offer"], 80, 805),
            button("MarketApplyOfferActionButton", "Apply", "MarketApplyOfferAction", 480, 805, True)], False)]
    receipt = [label("MarketReceiptTitle", "", 90, 458, 2020, 60, color="$FontColorGoldYellow", size=30),
        dropdown("MarketReceiptMode", ["Message / record", "Payment details"], 80, 550, 650),
        divider("MarketReceiptDivider", 80, 640, 2060),
        label("MarketReceiptDescription", "", 110, 690, 2010, 620, color="$FontColorBeige", size=28),
        button("MarketBackReceiptButton", "Back", "MarketBackReceipt", 80, 1390, True),
        button("MarketReceiptPrevButton", "Previous page", "MarketReceiptPrev", 600, 1390, True),
        button("MarketReceiptNextButton", "Next page", "MarketReceiptNext", 1774, 1390, True),
        label("MarketReceiptPage", "", 1040, 1405, 680, 45, True, color="$FontColorBeige", size=22)]
    receipt += [label(f"MarketReceiptTerm{n}", "", 110 + n // 8 * 1040, 690 + n % 8 * 80, 970, 70, size=26) for n in range(16)]
    focus = [button("MarketBackListButton", "Back to items", "MarketBackList", 80, 350, True),
        label("MarketDetailTitle", "", 480, 365, 1660, 44, color="$FontColorGoldYellow", size=26),
        divider("MarketFocusDivider", 80, 432, 2060),
        group("MarketItemHero", hero), group("MarketItemDetail", details), group("MarketPriceEditor", editor, False),
        group("MarketHistoryPanel", history, False), group("MarketThreadPanel", thread, False), group("MarketReceiptPanel", receipt, False)]
    return [group("MarketCollection", collection), group("MarketFocus", focus, False)]


def streamline(workspace):
    """Keep collections quiet; advanced choices get a dedicated native page."""
    nodes = {}
    def collect(node):
        nodes[node['name']] = node
        for child in node.get('children', []): collect(child)
    collect(workspace)
    def take(name):
        node = nodes[name]
        for parent in nodes.values():
            if node in parent.get('children', []): parent['children'].remove(node); break
        return node
    def move(name, x, y, w=None, h=None):
        r=nodes[name]['fields'].setdefault('rect', {})
        r.update(x=x,y=y)
        if w is not None: r['width']=w
        if h is not None: r['height']=h
    def field(name,x,y,w):
        old=take(name+'Field');new=input_box(name, nodes[name]['fields'].get('tooltipString',''), x,y,w,64 if name=='MarketSearch' else nodes[name]['fields'].get('maxStringLength',64))
        return new
    # Search stays visible. Everything else lives under Filters.
    search=field('MarketSearch',80,350,1570)
    nodes['MarketFilters']['children']=[search,button('MarketFilterOpenButton','Filters','MarketFilterOpen',1774,350,True),
        label('MarketCollectionHint','Choose an item to inspect its price and properties.',80,285,2060,40,color='$FontColorBeige',size=24)]
    own=take('MarketOwnFilters');afford=take('MarketAffordFilter');social=take('MarketSocialFilters')
    quality=take('MarketQuality');sort=take('MarketSort');tab=take('MarketTab');priced=take('MarketPriced');wallet=take('MarketWalletButton')
    scope=take('MarketSearchScope')
    # Rebuild dropdowns with clear labels and room for their complete caption.
    quality=dropdown('MarketQuality',quality['fields']['states'],80,600,600)
    sort=dropdown('MarketSort',sort['fields']['states'],780,600,600)
    tab=dropdown('MarketTab',tab['fields']['states'],1480,600,600)
    scope=dropdown('MarketSearchScope',scope['fields']['states'],80,760,600)
    seller=field('MarketSellerSearch',80,950,920)
    favorite=dropdown('MarketFavorites',nodes['MarketFavorites']['fields']['states'],1120,950,960)
    browse_options=group('MarketSocialFilters',[
        label('MarketSellerSearchTitle','SELLER',80,895,920,40,color='$FontColorGoldYellow',size=24),seller,
        label('MarketFavoritesTitle','FAVORITES',1120,895,960,40,color='$FontColorGoldYellow',size=24),favorite,
        button('MarketHideOwnButton','Hide my trades: Off','MarketHideOwn',610,1110),
        button('MarketHideOtherOffersButton','No competing offers: Off','MarketHideOtherOffers',1140,1110)])
    affordability=group('MarketAffordFilter',[button('MarketAffordable','Affordable only: Off','MarketAffordable',80,1110)])
    owned=group('MarketOwnFilters',[
        label('MarketTabTitle','STASH TAB',1480,540,600,40,color='$FontColorGoldYellow',size=24),tab,
        button('MarketPriced','Priced only: Off','MarketPriced',80,800,True)])
    filter_panel=group('MarketFilterPanel',[
        button('MarketFilterBackButton','Back to items','MarketFilterBack',80,350,True),
        label('MarketFilterTitle','REFINE YOUR SEARCH',500,362,1600,50,color='$FontColorGoldYellow',size=30),
        label('MarketQualityTitle','ITEM QUALITY',80,540,600,40,color='$FontColorGoldYellow',size=24),quality,
        label('MarketSortTitle','SORT BY',780,540,600,40,color='$FontColorGoldYellow',size=24),sort,
        label('MarketSearchScopeTitle','SEARCH IN',80,705,600,40,color='$FontColorGoldYellow',size=24),scope,
        owned,browse_options,affordability,
        button('MarketFilterResetButton','Reset filters','MarketFilterReset',80,1390,True),
        button('MarketFilterApplyButton','Show results','MarketFilterBack',1668,1385)],False)
    # Alias command is explicit; each JSON button retains its own bridge name.
    filter_panel['children'][-1]['fields']['onClickMessage']='BankPanelMessage:MarketFilterApply'
    nodes['MarketListGroup']['children'].append(filter_panel)
    wallet['fields']['rect'].update(x=1774,y=1390)
    nodes['MarketCollection']['children'].append(wallet)
    move('MarketNextButton',1370,1390)
    # Two secondary actions beside the item. Favorites/seller discovery remain on the website.
    nodes['MarketBrowseSocial']['children']=[nodes['MarketViewOffersButton'],nodes['MarketFavoriteTradeButton'],group('MarketUnusedSocial',[nodes['MarketFavoriteSellerButton'],nodes['MarketSellerTradesButton']],False)]
    move('MarketViewOffersButton',1774,458,366,68);move('MarketFavoriteTradeButton',1774,548,366,68)
    for name in ['MarketViewOffersButton','MarketFavoriteTradeButton']:
        nodes[name]['fields']['filename']=ART+'Buttons\\ButtonSmall_R'
        child=nodes[name]['children'][0];child['fields']['rect'].update(width=342,height=68)
        child['fields']['style']['pointSize']=22
    # Keep all useful controls native; reserve a consistently quiet footer.
    nodes['MarketStatus']['fields']['style']['pointSize']=22
    nodes['MarketItemExtra']['fields']['style']['pointSize']=20
    nodes['MarketReceiptTerm0']['fields']['rect']=rect(110,690,2010,620)
    nodes['MarketReceiptTerm0']['fields']['style']['pointSize']=28
    nodes['MarketSubtitle']['fields']['text']='PLAYER TRADING'


def polish(workspace):
    """Fit to the artwork's opaque area, including the actual button sprites."""
    nodes = {}
    def collect(node):
        nodes[node['name']] = node
        for child in node.get('children', []): collect(child)
    collect(workspace)
    def move(name, **values):
        nodes[name]['fields'].setdefault('rect', {}).update(values)
    def text(name, value, size=None):
        nodes[name]['fields']['text'] = value
        if size: nodes[name]['fields']['style']['pointSize'] = size
    # InfoBGW_R has transparent padding: the visible panel occupies x10..2238,
    # y60..1558. Keep every interactive control inside that area.
    move('MarketAnchor', y=-978)
    move('MarketShellEdge', x=10, y=60, width=2228, height=1498)
    move('MarketShellInset', x=12, y=62, width=2224, height=1494)
    move('MarketCloseButton', x=2139, y=62)
    move('MarketListingsPanelEdge', height=998)
    move('MarketListingsPanelInset', height=994)
    move('MarketFooterDivider', y=1474)
    move('MarketRefreshButton', y=1484)
    nodes['MarketAnchor']['children'].append(button('MarketSyncButton', 'Sync now', 'MarketSync', 1774, 1484, True))
    move('MarketStatusBackground', x=470, y=1484, width=1260, height=68)
    for name in ['MarketStatus', 'MarketEditNotice']:
        move(name, y=1488, height=58)
        move(name, width=1240)
        nodes[name]['fields']['style']['pointSize'] = 20
    for name in ['MarketPricePrevButton', 'MarketPriceNextButton', 'MarketSavePriceButton', 'MarketRemovePriceButton']:
        move(name, y=1390)
    move('MarketPrevButton', x=80)
    move('MarketNextButton', x=1774)
    # The profile form gets a full row and its own help line. Existing input
    # names and revisions stay intact for keyboard focus and command handling.
    text('MarketProfileTitle', 'PLAYER NAME', 24)
    move('MarketProfileTitle', x=80, y=360, width=1400, height=38)
    field = nodes['MarketProfileNameField']
    field['fields']['rect'] = rect(80, 412, 1100, 66)
    replacement = input_box('MarketProfileName', 'Public player name (3-15 characters)', 80, 412, 1100, 15)
    field['children'] = replacement['children']
    move('MarketSaveNameButton', x=1240, y=412, width=366, height=68)
    nodes['MarketSaveNameButton']['fields']['filename'] = ART + 'Buttons\\ButtonSmall_R'
    move('MarketProfileHint', x=80, y=500, width=2060, height=40)
    text('MarketProfileHint', '3-15 characters. Your player ID stays the same.', 22)
    # Move both page cards down together; leave a quiet gap before the toggles.
    setup = nodes['MarketSetupGroup']
    profile_names = {name for name in nodes if name.startswith('MarketProfile') or name.startswith('MarketSaveName')}
    def shift_cards(node):
        name = node['name']
        if name in profile_names: return
        r = node.get('fields', {}).get('rect', {})
        if 435 <= r.get('y', -1) < 800: r['y'] += 130
        for child in node.get('children', []): shift_cards(child)
    shift_cards(setup)
    for name in ['MarketTradingRowEdge', 'MarketTradingRowInset', 'MarketTradingIcon', 'MarketTradingTitle', 'MarketTradingHelp', 'MarketEnabled',
                 'MarketApprovalRowEdge', 'MarketApprovalRowInset', 'MarketApprovalMark', 'MarketApprovalTitle', 'MarketPolicy', 'MarketAuto']:
        nodes[name]['fields']['rect']['y'] += 130
    # Do not hide an important action under a floating caption or status line.
    move('MarketSaveSetupButton', x=1668, y=1385)
    move('MarketReloadButton', y=1394)
    move('MarketRouteNotice', x=80, y=1334, width=2060, height=44)
    nodes['MarketRouteNotice']['fields']['style']['pointSize'] = 20
    text('MarketWithdrawHelp', 'Priced items move into your Vault.', 24)
    # All portal reuses within this menu now use its own marketplace emblem.
    for node in nodes.values():
        f = node.get('fields', {})
        if f.get('filename') == ART + 'Shop\\Shop_Button':
            f['filename'] = MARKET_ART + 'MarketplaceButton'
        if node['type'] != 'ButtonWidget': continue
        filename = f.get('filename', '')
        if filename.endswith('ButtonMed_R') and f.get('rect', {}).get('width') == 366:
            filename = ART + 'Buttons\\ButtonSmall_R'
            f['filename'] = filename
        size = (366, 68) if filename.endswith('ButtonSmall_R') else (472, 87) if filename.endswith('ButtonMed_R') else None
        if not size: continue
        r = f['rect']
        # The legacy profile button was 366x68 but used 472x87 artwork.
        # Every stock button now has the same logical and rendered bounds.
        r.update(width=size[0], height=size[1])
        for caption in node.get('children', []):
            if caption['type'] != 'TextBoxWidget': continue
            caption['fields']['rect'] = rect(12, 0, size[0]-24, size[1])
            style = caption['fields']['style']
            style['alignment'] = {'h': 'center', 'v': 'center'}
            style['pointSize'] = 24 if size[0] == 366 else 26
            style['options']['lineWrap'] = False


def campaign_views():
    glow = group("MarketCampaignBadgeGlow", [
        fill("MarketCampaignGlowOuter", 1814, 181, 378, 80, [0.85, 0.58, 0.18, 0.10]),
        fill("MarketCampaignGlowMiddle", 1817, 184, 372, 74, [0.95, 0.72, 0.28, 0.16])])
    badge_button = button("MarketCampaignOpenButton", "Gifts available", "MarketCampaignOpen", 1820, 187, True)
    badge_button["children"][0]["fields"]["style"].update(fontColor="$FontColorGoldYellow", dropShadow={"shadowColor": {"r": 190, "g": 125, "b": 30, "a": 150}, "offset": {"x": 1, "y": 1}})
    badge = group("MarketCampaignBadge", [glow, badge_button,
        art("MarketCampaignBadgeIcon", MARKET_ART + "MarketplaceGift", 1834, 201, 0.6)], False)
    cards = []
    for n in range(4):
        x, y = 80 + n % 2 * 1060, 730 + n // 2 * 240
        cards.append(group(f"MarketCampaignCard{n}", frame(f"MarketCampaignCardFrame{n}", x, y, 1000, 220) + [
            widget("ImageWidget", f"MarketCampaignItemImage{n}", {"rect": rect(x + 20, y + 20, 112, 180), "filename": "Blank"}),
            item_link(f"MarketCampaignItemLink{n}", x + 165, y + 30, 795, 64),
            label(f"MarketCampaignItemFacts{n}", "", x + 165, y + 112, 795, 82, size=24)]))
    body = [art("MarketCampaignHeroIcon", MARKET_ART + "MarketplaceGift", 85, 510, 1.3),
        label("MarketCampaignFrom", "", 195, 530, 1130, 48, color="$FontColorGoldYellow", size=28),
        label("MarketCampaignExpiry", "", 1350, 534, 790, 48, size=24),
        label("MarketCampaignAnnouncement", "", 80, 600, 2060, 88, size=24),
        divider("MarketCampaignDivider", 80, 690, 2060),
        label("MarketCampaignItemsPage", "", 80, 1240, 950, 44, size=24),
        button("MarketCampaignItemsPrevButton", "Previous items", "MarketCampaignItemsPrev", 1350, 1235, True),
        button("MarketCampaignItemsNextButton", "Next items", "MarketCampaignItemsNext", 1774, 1235, True),
        label("MarketCampaignHelp", "", 80, 1335, 1400, 78, size=24),
        button("MarketCampaignClaimButton", "Claim gift", "MarketCampaignClaim", 1668, 1374)] + cards
    panel = group("MarketCampaignPanel", [button("MarketCampaignBackButton", "Back", "MarketCampaignBack", 80, 350, True),
        label("MarketCampaignTitle", "Community gifts", 490, 350, 1200, 70, color="$FontColorGoldYellow", size=30),
        label("MarketCampaignEmpty", "No gifts are currently available.", 80, 550, 2060, 100, True, size=28),
        label("MarketCampaignNumber", "", 1720, 350, 420, 44, True, size=24),
        button("MarketCampaignPrevButton", "Previous gift", "MarketCampaignPrev", 1350, 420, True),
        button("MarketCampaignNextButton", "Next gift", "MarketCampaignNext", 1774, 420, True),
        group("MarketCampaignBody", body)], False)
    return [badge, panel]


def build():
    title = frame("MarketShell", 24, 54, 2202, 1560) + [widget("ImageWidget", "MarketBackground", {"filename": ART + "Menus\\InfoBGW_R"}),
        art("MarketCrest", ART + "Shop\\Shop_Button", 80, 65, 0.55),
        label("MarketTitle", "MARKETPLACE", 210, 64, 1830, 52, True, size=36),
        label("MarketSubtitle", "ITEMS  |  BARTER  |  AGREEMENTS", 210, 117, 1830, 24, True, color="$FontColorGoldYellow", size=16),
        divider("MarketHeaderDivider", 60, 146, 2130),
        widget("ButtonWidget", "MarketCloseButton", {"rect": rect(2158, 14, 83, 84),
            "filename": ART + "Buttons\\Exit", "pressedFrame": 1, "hoveredFrame": 3,
            "onClickMessage": "BankPanelMessage:Close", "tooltipString": "@strClose",
            "acceptsEscKeyEverywhere": True, "sound": "cursor_close_window_hd"})]
    close = title.pop()  # Last child gets native input/Escape priority.
    title.append(widget("TabBarWidget", "MarketSections", {
        "rect": dict(rect(485, 170, 984, 75), scale=1.3), "tabCount": 4,
        "filename": ART + "Menus\\PanelTab_R",
        "inactiveFrames": [0] * 4, "activeFrames": [1] * 4,
        "textStrings": ["Browse", "Vault", "Offers & Logs", "Setup"],
        "textStyle": {"pointSize": "$SmallPanelFontSize"},
        "activeTextColor": "$TabsActiveTextColor", "inactiveTextColor": "$TabsInactiveTextColor",
        "tabSize": {"x": 249, "y": 75}, "tabPadding": {"x": -4, "y": 0},
        "tabMessages": ["BankPanelMessage:" + command for command in
                        ("MarketBrowse", "MarketItems", "MarketAgreements", "MarketSetup")]}))
    title += [divider("MarketFooterDivider", 60, 1504, 2130),
        fill("MarketStatusBackground", 460, 1520, 1710, 90, [0.018, 0.016, 0.013, 0.8]),
        button("MarketRefreshButton", "Refresh", "MarketRefresh", 80, 1530, True),
        label("MarketStatus", "No items transferred.", 470, 1528, 1660, 80)]
    filters = [label("MarketSearchTitle", "Search", 80, 350, 160, 58),
        input_box("MarketSearch", "Search items, sellers or payment", 250, 345, 1900),
        dropdown("MarketQuality", ["All qualities", "Unique", "Set", "Magic", "Rare", "Runes"], 80, 425),
        dropdown("MarketSort", ["Name: A-Z", "Quality: high-low", "Seller / Tab: A-Z", "Payment types", "Name: Z-A", "Quality: low-high", "Seller / Tab: Z-A"], 470, 425),
        group("MarketAffordFilter", [button("MarketAffordable", "Show things I can afford", "MarketAffordable", 860, 425)]),
        group("MarketOwnFilters", [dropdown("MarketTab", ["All tabs"] + [f"Tab {n}" for n in range(1, 8)], 860, 425), button("MarketPriced", "Priced only: Off", "MarketPriced", 1250, 425, True), button("MarketWalletButton", "Vault", "MarketWallet", 1668, 425, True)])]
    content = focused_views(filters)
    setup = [label("MarketSetupTitle", "TRADE SETUP", 80, 290, 2060, 50, color="$FontColorGoldYellow", size=30),
        label("MarketSetupIntro", "Choose your stash pages. These settings are remembered.", 80, 355, 2060, 45, color="$FontColorBeige", size=24)]
    for name, x, icon, natural, heading, help_text in [
            ("Deposit", 80, "items/custom/GoldBar", 196, "DEPOSIT", "Items for sale and payment"),
            ("Withdraw", 1140, "items/custom/gold_m", 98, "DELIVERY", "Purchases and proceeds")]:
        setup += frame("Market" + name + "Route", x, 435, 1000, 355)
        setup += frame("Market" + name + "RouteWell", x + 28, 470, 130, 140)
        setup += [art("Market" + name + "RouteIcon", icon, x + 42, 488, 102 / natural),
            label("Market" + name + "Title", heading, x + 195, 478, 770, 50, color="$FontColorGoldYellow", size=28),
            label("Market" + name + "Help", help_text, x + 195, 548, 770, 50, color="$FontColorBeige", size=24),
            divider("Market" + name + "RouteDivider", x + 28, 630, 944),
            label("Market" + name + "PageTitle", "Page", x + 35, 693, 130, 45, size=26),
            input_box(name + "Page", "Stash page, 1 to 64", x + 190, 680, 240),
            button("Market" + name + "Current", "Use current page", "Market" + name + "Current", x + 600, 680, True)]
    setup += frame("MarketTradingRow", 80, 850, 2060, 150)
    setup += frame("MarketApprovalRow", 80, 1040, 2060, 150)
    setup += [art("MarketTradingIcon", ART + "Shop\\Shop_Button", 110, 893, .55),
        label("MarketTradingTitle", "TRADING", 220, 875, 1400, 42, color="$FontColorGoldYellow", size=26),
        label("MarketTradingHelp", "Offer items from your deposit", 220, 933, 1400, 40, color="$FontColorBeige", size=24),
        button("MarketEnabled", "Disabled", "MarketEnabled", 1740, 891, True),
        fill("MarketApprovalMark", 134, 1098, 18, 24, [.64, .46, .21, 1]),
        label("MarketApprovalTitle", "APPROVAL", 220, 1065, 1400, 42, color="$FontColorGoldYellow", size=26),
        label("MarketPolicy", "Review each offer yourself", 220, 1123, 1400, 40, color="$FontColorBeige", size=24),
        button("MarketAuto", "Manual", "MarketAuto", 1740, 1081, True),
        label("MarketRouteNotice", "", 80, 1240, 2060, 80, color="$FontColorBeige", size=24),
        button("MarketSaveSetupButton", "Save changes", "MarketSaveSetup", 1668, 1360),
        button("MarketReloadButton", "Undo changes", "MarketReload", 80, 1369, True)]
    wallet_rows = []
    for n in range(4):
        y = 570 + n * 180
        row = frame(f"MarketWalletRow{n}", 80, y, 2060, 155) + [
            label(f"MarketWalletRow{n}Title", "", 110, y + 16, 1150, 44, color="$FontColorGoldYellow", size=28),
            label(f"MarketWalletRow{n}Detail", "", 110, y + 68, 1150, 74, color="$FontColorBeige", size=23),
            button(f"MarketWalletGift{n}Button", "Gift", f"MarketWalletGift{n}", 1340, y + 42, True),
            button(f"MarketWalletAction{n}Button", "Withdraw", f"MarketWalletAction{n}", 1740, y + 42, True)]
        wallet_rows.append(group(f"MarketWalletRow{n}", row))
    wallet_rows += [label("MarketWalletEmpty", "Nothing here yet.", 110, 660, 1960, 90),
        button("MarketWalletPrevButton", "Previous", "MarketWalletPrev", 80, 1350, True),
        button("MarketWalletNextButton", "Next", "MarketWalletNext", 1774, 1350, True),
        label("MarketWalletPage", "", 650, 1362, 950, 44, True, size=24)]
    wallet_form = [label("MarketWalletFormTitle", "", 110, 580, 1980, 52, color="$FontColorGoldYellow", size=30),
        label("MarketWalletAsset", "", 110, 670, 1980, 110, size=28),
        label("MarketWalletAmountTitle", "Amount to return", 110, 815, 700, 44, color="$FontColorGoldYellow", size=26),
        input_box("MarketWalletAmount", "Whole amount, up to 2000000000", 110, 875, 800, 10),
        label("MarketWalletReviewAmount", "", 110, 830, 1980, 90, color="$FontColorGoldYellow", size=32),
        divider("MarketWalletFormDivider", 110, 1000, 1980),
        label("MarketWalletFormHelp", "", 110, 1060, 1980, 160, color="$FontColorBeige", size=28),
        button("MarketWalletSubmitButton", "Review amount", "MarketWalletSubmit", 1668, 1330)]
    wallet = [button("MarketWalletBackButton", "My items", "MarketWalletBack", 80, 350, True),
        label("MarketWalletTitle", "VAULT", 500, 362, 1600, 50, color="$FontColorGoldYellow", size=30),
        label("MarketWalletHelp", "", 80, 442, 2060, 100, color="$FontColorBeige", size=24),
        dropdown("MarketWalletMode", ["Available balances", "Withdrawals", "Activity"], 80, 270, 800),
        group("MarketWalletRows", wallet_rows), group("MarketWalletForm", wallet_form, False)]
    gift = [button("MarketGiftBackButton", "Back", "MarketGiftBack", 80, 350, True),
        label("MarketGiftTitle", "SEND A GIFT", 500, 362, 1600, 50, color="$FontColorGoldYellow", size=30),
        label("MarketGiftAsset", "", 110, 540, 1980, 100, color="$FontColorGoldYellow", size=30),
        label("MarketGiftHelp", "", 110, 660, 1980, 110, color="$FontColorBeige", size=26),
        label("MarketGiftRecipientTitle", "Exact linked player name", 110, 800, 1800, 44, color="$FontColorGoldYellow", size=26),
        input_box("MarketGiftRecipient", "Player name", 110, 855, 1800, 64),
        label("MarketGiftAmountTitle", "Amount to gift", 110, 970, 1800, 44, color="$FontColorGoldYellow", size=26),
        input_box("MarketGiftAmount", "Whole amount", 110, 1025, 800, 10),
        label("MarketGiftNoteTitle", "Optional message", 110, 1140, 1800, 44, color="$FontColorGoldYellow", size=26),
        input_box("MarketGiftNote", "A note to go with the gift", 110, 1195, 1800, 256),
        label("MarketGiftReview", "", 110, 800, 1980, 450, color="$FontColorGoldYellow", size=30),
        button("MarketGiftSubmitButton", "Review recipient", "MarketGiftSubmit", 1668, 1400)]
    title += [group("MarketListGroup", content), group("MarketSetupGroup", setup, False), group("MarketWalletGroup", wallet, False), group("MarketVaultNavigation", [dropdown("MarketVaultView", ["Items", "Gold & resources", "Withdrawals", "Activity"], 80, 270, 900)], False), group("MarketGiftGroup", gift, False), close]
    workspace = widget("RectangleWidget", "MarketplaceWorkspace", {"fitToScreen": True, "priority": 9002, "visible": False, "color": [0, 0, 0, 0.7]}, [
        widget("ClickCatcherWidget", "MarketCatcher", {"fitToParent": True}),
        widget("Widget", "MarketAnchor", {"anchor": {"x": 0.5, "y": 0.5}, "rect": rect(-1125, -818, 2250, 1635)}, title)])
    # Social/physical-withdrawal controls were authored after the original
    # generator. Keep their reviewed native layouts reproducible on rebuilds.
    patch = json.loads((ROOT / "marketplace-social-patch.json").read_text(encoding="utf-8"))
    nodes = {}
    def collect(node):
        nodes[node["name"]] = node
        for child in node.get("children", []):
            collect(child)
    collect(workspace)
    for name, fields in patch["fields"].items():
        nodes[name]["fields"] = fields
    for entry in patch["upsert"]:
        parent = nodes[entry["parent"]]
        parent.setdefault("children", []).append(entry["widget"])
    streamline(workspace)
    # Keep the single market-page UI reproducible as well as the social widgets.
    single = json.loads((ROOT / "marketplace-single-page-patch.json").read_text(encoding="utf-8"))
    nodes.clear()
    collect(workspace)
    for name, fields in single["fields"].items():
        nodes[name]["fields"] = fields
    def remove_obsolete(node):
        node["children"] = [c for c in node.get("children", []) if c["name"] not in single["remove"]]
        for child in node["children"]:
            remove_obsolete(child)
    remove_obsolete(workspace)
    for entry in single["upsert"]:
        nodes[entry["parent"]].setdefault("children", []).append(entry["widget"])
    nodes["MarketAnchor"]["children"][-1:-1] = campaign_views()
    polish(workspace)
    shop = widget("ButtonWidget", "MarketplaceShop", {"rect": rect(1475, 1326, 117, 116), "filename": MARKET_ART + "MarketplaceButton",
        "pressedFrame": 1, "hoveredFrame": 2, "tooltipString": "Marketplace", "onClickMessage": "BankPanelMessage:MarketOpen"})
    return shop, workspace


if __name__ == "__main__":
    ROOT.mkdir(exist_ok=True)
    for name, value in zip(("stash-shop-button.json", "marketplace-workspace.json"), build()):
        (ROOT / name).write_text(json.dumps(value, indent=4) + "\n", encoding="utf-8")
