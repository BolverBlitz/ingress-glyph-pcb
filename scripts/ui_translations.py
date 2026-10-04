"""Build-time UI localization. Only the selected language enters each firmware."""

import json
import re


# English source text -> (German, Japanese). Keep device/API identifiers out of
# this table; it is applied only to HTML text nodes, selected attributes, and
# complete JavaScript string literals.
TEXT = {
    "Primary navigation": ("Hauptnavigation", "メインナビゲーション"),
    "Main": ("Start", "メイン"),
    "Draw": ("Zeichnen", "描画"),
    "Nearby": ("In der Nähe", "近く"),
    "Settings": ("Einstellungen", "設定"),
    "Need Help?": ("Hilfe?", "ヘルプ"),
    "Help topics": ("Hilfethemen", "ヘルプ項目"),
    "Buttons &amp; Features": ("Tasten &amp; Funktionen", "ボタンと機能"),
    "Buttons and Features help": ("Hilfe zu Tasten und Funktionen", "ボタンと機能のヘルプ"),
    "Settings help": ("Hilfe zu Einstellungen", "設定のヘルプ"),
    "Back to Settings": ("Zurück zu Einstellungen", "設定に戻る"),
    "Mode": ("Modus", "モード"),
    "Playback mode": ("Wiedergabemodus", "再生モード"),
    "Glyph Sequence": ("Glyphenfolge", "グリフシーケンス"),
    "Random Glyphs": ("Zufällige Glyphen", "ランダムなグリフ"),
    "Glyph sequence": ("Glyphenfolge", "グリフシーケンス"),
    "Known sequence": ("Bekannte Folge", "既知のシーケンス"),
    "Build your own": ("Eigene erstellen", "自分で作成"),
    "Find sequence by name": ("Folge nach Namen suchen", "名前でシーケンスを検索"),
    "Search sequence names": ("Folgenamen suchen", "シーケンス名を検索"),
    "Find by glyph": ("Nach Glyphe suchen", "グリフで検索"),
    "Any glyph": ("Beliebige Glyphe", "すべてのグリフ"),
    "Visual sequences": ("Visuelle Folgen", "図で表示"),
    "Names": ("Namen", "名前"),
    "Known sequences": ("Bekannte Folgen", "既知のシーケンス"),
    "Find by name": ("Nach Namen suchen", "名前で検索"),
    "Draw glyph": ("Glyphe zeichnen", "グリフを描く"),
    "Glyph name": ("Glyphenname", "グリフ名"),
    "Search glyph name": ("Glyphennamen suchen", "グリフ名を検索"),
    "Matching glyphs": ("Passende Glyphen", "一致するグリフ"),
    "Add": ("Hinzufügen", "追加"),
    "Draw a glyph by connecting highlighted nodes": ("Glyphe durch Verbinden der markierten Punkte zeichnen", "強調された点をつないでグリフを描く"),
    "Drag through the nodes in one stroke. Direction does not matter.": ("Ziehe in einem Zug über die Punkte. Die Richtung ist egal.", "点を一筆書きでなぞります。方向は問いません。"),
    "Glyphs matching the drawing": ("Zur Zeichnung passende Glyphen", "描画に一致するグリフ"),
    "Add match": ("Treffer hinzufügen", "一致を追加"),
    "Clear drawing": ("Zeichnung löschen", "描画を消去"),
    "Up to five glyph IDs. Use the arrows to reorder them.": ("Bis zu fünf Glyphen. Mit den Pfeilen sortieren.", "グリフは最大5個です。矢印で並べ替えます。"),
    "A new random glyph is selected after every interval.": ("Nach jedem Intervall wird eine neue zufällige Glyphe gewählt.", "間隔ごとに新しいランダムなグリフを選びます。"),
    "Saved": ("Gespeichert", "保存済み"),
    "Draw pages": ("Zeichenseiten", "描画ページ"),
    "play in their current order.": ("werden in ihrer aktuellen Reihenfolge abgespielt.", "は現在の順序で再生されます。"),
    "Switch interval (seconds)": ("Wechselintervall (Sekunden)", "切り替え間隔（秒）"),
    "Glyph colors": ("Glyphenfarben", "グリフの色"),
    "White mix": ("Weißanteil", "白の混合"),
    "Brightness": ("Helligkeit", "明るさ"),
    "Line": ("Linie", "線"),
    "Core": ("Kern", "中心"),
    "White": ("Weiß", "白"),
    "Level": ("Stärke", "レベル"),
    "Line RGB": ("Linie RGB", "線のRGB"),
    "Line white mix": ("Weißanteil der Linie", "線の白の混合"),
    "Line brightness": ("Linienhelligkeit", "線の明るさ"),
    "Core RGB": ("Kern RGB", "中心のRGB"),
    "Core white mix": ("Weißanteil des Kerns", "中心の白の混合"),
    "Core brightness": ("Kernhelligkeit", "中心の明るさ"),
    "Effects": ("Effekte", "エフェクト"),
    "Effect": ("Effekt", "エフェクト"),
    "Off": ("Aus", "オフ"),
    "Fade": ("Überblenden", "フェード"),
    "Fade settings": ("Überblendeinstellungen", "フェード設定"),
    "Direction": ("Richtung", "方向"),
    "Global": ("Gesamt", "全体"),
    "Center outward": ("Von der Mitte nach außen", "中心から外側へ"),
    "Center inward": ("Von außen zur Mitte", "外側から中心へ"),
    "Down": ("Nach unten", "下へ"),
    "Up": ("Nach oben", "上へ"),
    "Left": ("Nach links", "左へ"),
    "Right": ("Nach rechts", "右へ"),
    "Fade duration (ms)": ("Überblenddauer (ms)", "フェード時間（ms）"),
    "Type": ("Typ", "種類"),
    "Quickfade": ("Schnelles Überblenden", "クイックフェード"),
    "Slowfade": ("Langsames Überblenden", "スローフェード"),
    "Mix": ("Mischen", "ミックス"),
    "Quickfade fades each LED out, then in, with overlapping waves. Slowfade fades the whole display out before fading the next frame in. Mix blends frames directly.": ("Schnelles Überblenden blendet jede LED erst aus und dann ein, mit überlappenden Wellen. Langsames Überblenden blendet die gesamte Anzeige aus, bevor das nächste Bild eingeblendet wird. Mischen blendet die Bilder direkt ineinander über.", "クイックフェードは波を重ねながら各LEDを消灯してから点灯します。スローフェードは表示全体を消灯してから次のフレームを点灯します。ミックスはフレームを直接混ぜ合わせます。"),
    "Duration covers the entire transition. The switch interval holds each completed frame.": ("Die Dauer umfasst den gesamten Übergang. Das Wechselintervall bestimmt, wie lange jedes fertige Bild angezeigt wird.", "時間は切り替え全体の長さです。切り替え間隔は、切り替え後の各フレームを表示する時間です。"),
    "Global fades all LEDs together, without a moving wave.": ("Gesamt blendet alle LEDs gleichzeitig über, ohne bewegte Welle.", "全体では、波を動かさずにすべてのLEDを同時にフェードします。"),
    "Controller time is not synchronized.": ("Die Controllerzeit ist nicht synchronisiert.", "コントローラーの時刻が同期されていません。"),
    "Sync browser time": ("Browserzeit übernehmen", "ブラウザーの時刻を同期"),
    "Glyph Drawer": ("Glyphenzeichner", "グリフ描画"),
    "Draw page": ("Zeichenseite", "描画ページ"),
    "Add page": ("Seite hinzufügen", "ページを追加"),
    "Rename": ("Umbenennen", "名前を変更"),
    "Delete page": ("Seite löschen", "ページを削除"),
    "Clear": ("Leeren", "消去"),
    "LED drawing canvas": ("LED-Zeichenfläche", "LED描画キャンバス"),
    "Color and tools": ("Farbe und Werkzeuge", "色とツール"),
    "Eraser": ("Radierer", "消しゴム"),
    "Save drawings": ("Zeichnungen speichern", "描画を保存"),
    "Save": ("Speichern", "保存"),
    "Nearby Agents": ("Agenten in der Nähe", "近くのエージェント"),
    "Nearby agents": ("Agenten in der Nähe", "近くのエージェント"),
    "Scanning...": ("Suche läuft ...", "スキャン中…"),
    "Currently nearby": ("Gerade in der Nähe", "現在近くにいる人"),
    "Known agents": ("Bekannte Agenten", "既知のエージェント"),
    "Search by username": ("Nach Benutzernamen suchen", "ユーザー名で検索"),
    "Agent name": ("Agentenname", "エージェント名"),
    "Glyph Settings": ("Glyphen-Einstellungen", "グリフ設定"),
    "Global settings": ("Allgemeine Einstellungen", "全般設定"),
    "Username": ("Benutzername", "ユーザー名"),
    "Faction": ("Fraktion", "陣営"),
    "Time format": ("Zeitformat", "時刻表示"),
    "24-hour": ("24 Stunden", "24時間表示"),
    "12-hour": ("12 Stunden", "12時間表示"),
    "Hardware revision": ("Hardwareversion", "ハードウェアのリビジョン"),
    "LED display rotation": ("LED-Anzeige drehen", "LED表示の回転"),
    "Bluetooth proximity broadcasting": ("Bluetooth-Näherungssignal senden", "Bluetooth近接信号を送信"),
    "Friend Mode: display nearby friends' Agent Glyphs": ("Freundemodus: Agenten-Glyphen naher Freunde anzeigen", "フレンドモード：近くの友達のエージェントグリフを表示"),
    "Minimum watts": ("Mindestwert in Watt", "最小ワット数"),
    "Max LED power": ("Maximale LED-Leistung", "LEDの最大電力"),
    "Wi-Fi profiles": ("WLAN-Profile", "Wi-Fiプロファイル"),
    "Scan nearby Wi-Fi": ("WLANs suchen", "近くのWi-Fiを検索"),
    "Clear this Wi-Fi": ("Dieses WLAN löschen", "このWi-Fiを削除"),
    "Fallback hotspot": ("Ersatz-Hotspot", "代替ホットスポット"),
    "Used only when neither saved Wi-Fi profile can connect. Restart after saving changes.": ("Wird nur verwendet, wenn keines der gespeicherten WLANs erreichbar ist. Nach Änderungen neu starten.", "保存済みのWi-Fiに接続できない場合のみ使用します。変更を保存した後、再起動してください。"),
    "Default IP address:": ("Standard-IP-Adresse:", "標準IPアドレス："),
    "Hotspot name": ("Hotspot-Name", "ホットスポット名"),
    "Hotspot password": ("Hotspot-Passwort", "ホットスポットのパスワード"),
    "Leave blank to keep current password": ("Leer lassen, um das aktuelle Passwort zu behalten", "現在のパスワードを維持するには空欄にします"),
    "Device status": ("Gerätestatus", "デバイスの状態"),
    "Hardware": ("Hardware", "ハードウェア"),
    "ESP chip temperature": ("ESP-Chiptemperatur", "ESPチップ温度"),
    "Center LED temperature": ("LED-Temperatur Mitte", "中央LED温度"),
    "Bottom board temperature": ("Platinentemperatur unten", "基板下部の温度"),
    "Hardware LED limit": ("Hardware-LED-Grenze", "ハードウェアLED上限"),
    "Current LED power": ("Aktuelle LED-Leistung", "現在のLED電力"),
    "UTC time": ("UTC-Zeit", "UTC時刻"),
    "Checking controller time...": ("Controllerzeit wird geprüft ...", "コントローラーの時刻を確認中…"),
    "Checking NTP status...": ("NTP-Status wird geprüft ...", "NTPの状態を確認中…"),
    "Storage": ("Speicher", "ストレージ"),
    "Main display": ("Hauptanzeige", "メイン表示"),
    "Known people": ("Bekannte Personen", "既知のユーザー"),
    "Encounter history": ("Begegnungsverlauf", "遭遇履歴"),
    "Other / filesystem overhead": ("Sonstiges / Dateisystem", "その他／ファイルシステムの使用分"),
    "Available": ("Verfügbar", "空き容量"),
    "Export user list": ("Benutzerliste exportieren", "ユーザー一覧をエクスポート"),
    "Export encounter history": ("Begegnungsverlauf exportieren", "遭遇履歴をエクスポート"),
    "Reset users &amp; history": ("Benutzer &amp; Verlauf löschen", "ユーザーと履歴をリセット"),
    "Firmware update (OTA)": ("Firmware-Update (OTA)", "ファームウェア更新（OTA）"),
    "Download release": ("Version herunterladen", "リリースをダウンロード"),
    "Select a downloaded": ("Wähle eine heruntergeladene", "ダウンロードした"),
    ". Global settings and draw pages are retained. The controller restarts automatically after a successful update.": (". Allgemeine Einstellungen und Zeichenseiten bleiben erhalten. Nach dem Update startet der Controller automatisch neu.", "を選択します。設定と描画ページは保持されます。更新後、コントローラーは自動的に再起動します。"),
    "Firmware file": ("Firmware-Datei", "ファームウェアファイル"),
    "Install firmware": ("Firmware installieren", "ファームウェアをインストール"),
    "Restart controller": ("Controller neu starten", "コントローラーを再起動"),
    "Mode control": ("Modussteuerung", "モード操作"),
    "Flashlight": ("Taschenlampe", "ライト"),
    "Add a friend": ("Freund hinzufügen", "友達を追加"),
    "Nearby page friendship requests": ("Freundschaftsanfragen auf der Seite In der Nähe", "近くのページのフレンド申請"),
    "Choose Send Friendship Request for a nearby agent. The request stays pending for five minutes. The other board can choose Accept Request; after acceptance both boards show the normal five-second rainbow pairing animation. Pending requests can be cancelled, and existing friends can be removed. Requests are checked while Bluetooth is enabled and the boards are nearby.": ("Wähle Freundschaftsanfrage senden für einen Agenten in der Nähe. Die Anfrage bleibt fünf Minuten ausstehend. Das andere Gerät kann Anfrage annehmen wählen; danach zeigen beide die normale fünfsekündige Regenbogen-Kopplungsanimation. Ausstehende Anfragen können abgebrochen und bestehende Freunde entfernt werden. Anfragen werden geprüft, wenn Bluetooth aktiviert ist und die Geräte in der Nähe sind.", "近くのエージェントにフレンド申請を送るを選びます。申請は5分間保留されます。相手のボードで申請を承認すると、両方のボードで通常の5秒間のレインボーペアリングアニメーションが始まります。保留中の申請はキャンセルでき、既存の友達は削除できます。申請の確認にはBluetoothが有効で、ボードが近くにある必要があります。"),
    "Bluetooth proximity must be enabled on both devices. Physical Button 2 pairing remains supported. You can also manage friends on the": ("Bluetooth-Näherung muss auf beiden Geräten aktiviert sein. Die Kopplung mit Taste 2 bleibt verfügbar. Freunde kannst du auch auf der", "両方のデバイスでBluetooth近接機能を有効にしてください。物理ボタン2によるペアリングも引き続き利用できます。友達は"),
    "Emergency signal": ("Notsignal", "緊急信号"),
    "Your personal Agent Glyph": ("Deine persönliche Agenten-Glyphe", "自分のエージェントグリフ"),
    "A quick guide to the two buttons on your device and your personal drawing.": ("Kurzanleitung für die beiden Tasten und deine persönliche Zeichnung.", "2つのボタンと自分の描画についての簡単なガイドです。"),
    "Button 1 · press twice quickly": ("Taste 1 · zweimal schnell drücken", "ボタン1・素早く2回押す"),
    "Switch between your chosen glyph sequence, random glyphs, and your Draw pages. The new display mode is saved after 10 seconds without button presses.": ("Wechsle zwischen deiner Glyphenfolge, zufälligen Glyphen und deinen Zeichenseiten. Der neue Modus wird nach 10 Sekunden ohne Tastendruck gespeichert.", "選んだグリフシーケンス、ランダムなグリフ、描画ページを切り替えます。10秒間ボタンを押さないと、新しい表示モードが保存されます。"),
    "Button 1 · hold 2 seconds": ("Taste 1 · 2 Sekunden halten", "ボタン1・2秒間長押し"),
    "Turn on the flashlight. Hold Button 1 for 2 seconds again to turn it off and return to your display.": ("Schalte die Taschenlampe ein. Halte Taste 1 erneut 2 Sekunden, um sie auszuschalten und zur Anzeige zurückzukehren.", "ライトを点灯します。もう一度ボタン1を2秒間長押しすると消灯し、元の表示に戻ります。"),
    "Button 2 · press while flashlight is on": ("Taste 2 · bei eingeschalteter Taschenlampe drücken", "ライト点灯中にボタン2を押す"),
    "Switch flashlight brightness between 25%, 60%, and 100%.": ("Wechsle die Taschenlampenhelligkeit zwischen 25 %, 60 % und 100 %.", "ライトの明るさを25%、60%、100%に切り替えます。"),
    "Button 2 · hold on both devices": ("Taste 2 · auf beiden Geräten halten", "両方のデバイスでボタン2を長押し"),
    "Bring the devices close together and hold Button 2 on each for at least 5 seconds. They pair as friends and exchange their personal drawings when available.": ("Bringe die Geräte nahe zusammen und halte auf beiden Taste 2 mindestens 5 Sekunden. Sie werden Freunde und tauschen ihre persönlichen Zeichnungen aus, falls vorhanden.", "デバイスを近づけ、両方でボタン2を5秒以上長押しします。友達としてペアになり、利用可能なら個人の描画を交換します。"),
    "Bluetooth proximity must be enabled on both devices. You can also manage friends on the": ("Bluetooth-Näherung muss auf beiden Geräten aktiv sein. Freunde kannst du auch auf der", "両方のデバイスでBluetooth近接機能を有効にしてください。友達は"),
    "Nearby page": ("Seite „In der Nähe“", "「近く」ページ"),
    ".": (".", "。"),
    "Both buttons · hold 10 seconds": ("Beide Tasten · 10 Sekunden halten", "両方のボタン・10秒間長押し"),
    "Start a flashing white SOS signal and alert nearby devices.": ("Starte ein weiß blinkendes SOS-Signal und benachrichtige Geräte in der Nähe.", "白く点滅するSOS信号を開始し、近くのデバイスに通知します。"),
    "Both buttons · hold 2 seconds again": ("Beide Tasten · erneut 2 Sekunden halten", "両方のボタン・もう一度2秒間長押し"),
    "Stop the SOS signal.": ("Beende das SOS-Signal.", "SOS信号を停止します。"),
    "This is a local signal. It does not call emergency services.": ("Dies ist nur ein lokales Signal. Es ruft keinen Notdienst.", "これはローカルの信号です。緊急通報は行いません。"),
    "One Draw page can act like your profile picture when devices exchange drawings.": ("Eine Zeichenseite kann beim Austausch wie dein Profilbild dienen.", "描画ページの1つを、デバイス間で交換するプロフィール画像のように使えます。"),
    "Set your": ("Lege deinen", "設定で"),
    "on the": ("auf der", "を設定し、"),
    "Settings page": ("Einstellungsseite", "設定ページ"),
    "and save it.": ("fest und speichere ihn.", "保存します。"),
    "On the": ("Auf der", ""),
    "Draw page": ("Zeichenseite", "描画ページ"),
    ", add or rename a page to": ("fügst du eine Seite hinzu oder benennst sie", "でページを追加するか、名前を"),
    "exactly the same username": ("genau wie deinen Benutzernamen", "ユーザー名と完全に同じ名前"),
    "Draw your design, then tap": ("Zeichne dein Motiv und tippe dann auf", "デザインを描き、"),
    "on the Draw page.": ("auf der Zeichenseite.", "をタップします。"),
    "Capital letters and spaces must match. For example, username": ("Großbuchstaben und Leerzeichen müssen übereinstimmen. Zum Beispiel braucht der Benutzername", "大文字とスペースも一致する必要があります。たとえばユーザー名が"),
    "needs a Draw page named": ("eine Zeichenseite mit dem Namen", "なら、描画ページの名前も"),
    ", not": (", nicht", "でなければならず、"),
    ". Without a matching saved page, there is no personal drawing to share. Friends can see it during pairing and in Friend Mode.": (". Ohne passende gespeicherte Seite gibt es keine persönliche Zeichnung zum Teilen. Freunde sehen sie beim Koppeln und im Freundemodus.", "ではありません。一致する保存済みページがなければ、共有する個人の描画はありません。友達はペアリング中とフレンドモードで確認できます。"),
    "What each option does, in plain language. Tap": ("Alle Optionen einfach erklärt. Tippe nach Änderungen auf", "各項目を簡単に説明します。変更後は"),
    "Save changes": ("Änderungen speichern", "変更を保存"),
    "on the Settings page after editing.": ("auf der Einstellungsseite.", "をタップしてください。"),
    "Your identity": ("Deine Identität", "ユーザー情報"),
    "The name nearby devices see. A saved Draw page with exactly the same name becomes your personal Agent Glyph.": ("Diesen Namen sehen Geräte in der Nähe. Eine gespeicherte Zeichenseite mit genau diesem Namen wird deine persönliche Agenten-Glyphe.", "近くのデバイスに表示される名前です。同じ名前の保存済み描画ページが自分のエージェントグリフになります。"),
    "Choose Resistance or Enlightened. This changes the app theme and the faction shown to nearby devices.": ("Wähle Resistance oder Enlightened. Das ändert das App-Design und die Fraktion, die Geräte in der Nähe sehen.", "ResistanceかEnlightenedを選びます。アプリのテーマと近くのデバイスに表示される陣営が変わります。"),
    "Choose 12-hour or 24-hour times in the web app. It does not change the controller's clock.": ("Wähle 12- oder 24-Stunden-Anzeige in der Web-App. Die Controlleruhr bleibt unverändert.", "Webアプリで12時間表示か24時間表示を選びます。コントローラーの時計は変わりません。"),
    "Board &amp; display": ("Platine &amp; Anzeige", "基板と表示"),
    "Choose the revision of your board. Rev 1 uses a 4.5 W LED safety cap. Rev 2 uses an 8 W cap and two board-temperature sensors. Changing this setting saves it and restarts the controller.": ("Wähle die Version deiner Platine. Rev 1 begrenzt die LEDs auf 4,5 W. Rev 2 erlaubt bis zu 8 W und nutzt zwei Temperatursensoren. Eine Änderung speichert die Einstellung und startet den Controller neu.", "基板のリビジョンを選びます。Rev 1のLED安全上限は4.5 W、Rev 2は8 Wで温度センサーを2つ使います。変更を保存するとコントローラーが再起動します。"),
    "Choose 0&deg; or 180&deg; so the LEDs look upright for the way your board is mounted. This rotates the output; it does not change your saved drawings.": ("Wähle 0&deg; oder 180&deg;, damit die LEDs zur Montageausrichtung passen. Gespeicherte Zeichnungen werden nicht verändert.", "基板の取り付け方向に合わせて0&deg;か180&deg;を選びます。表示のみが回転し、保存済みの描画は変わりません。"),
    "Adjusts the normal glyph and Draw display. Flashlight brightness has its own control.": ("Regelt die normale Glyphen- und Zeichenanzeige. Die Taschenlampe hat eine eigene Helligkeitsregelung.", "通常のグリフ表示と描画表示を調整します。ライトの明るさは別に設定できます。"),
    "Bluetooth &amp; friends": ("Bluetooth &amp; Freunde", "Bluetoothと友達"),
    "Lets this device announce itself and find nearby devices. Leave it on for the Nearby page, friend pairing, and local emergency alerts.": ("Damit kündigt sich das Gerät an und findet andere Geräte in der Nähe. Für die Seite „In der Nähe“, das Koppeln von Freunden und lokale Notmeldungen eingeschaltet lassen.", "このデバイスを周囲に知らせ、近くのデバイスを探します。「近く」ページ、友達のペアリング、ローカルの緊急通知に必要です。"),
    "Friend Mode": ("Freundemodus", "フレンドモード"),
    "When a saved friend is nearby and their Agent Glyph is available, show friends' drawings on the LEDs instead of the normal display. Friendship is checked once when a new visit is recorded, regardless of Friend Mode. Removing a friend on one board notifies the other board immediately when nearby; if it is offline, the removal is reconciled at the next recorded visit. The drawings cycle while those friends are in range.": ("Wenn ein gespeicherter Freund mit Agenten-Glyphe in der Nähe ist, zeigen die LEDs statt der normalen Anzeige dessen Zeichnungen. Die Freundschaft wird unabhängig vom Freundemodus einmal geprüft, wenn ein neuer Besuch aufgezeichnet wird. Das Entfernen auf einem Gerät wird dem anderen Gerät in der Nähe sofort mitgeteilt; ist es offline, wird die Entfernung beim nächsten aufgezeichneten Besuch abgeglichen. Solange Freunde in Reichweite sind, wechseln die Zeichnungen.", "保存済みの友達が近くにいてエージェントグリフが利用できる場合、通常の表示の代わりに友達の描画をLEDに表示します。フレンドモードに関係なく、新しい遭遇が記録されたときにフレンド状態を1回確認します。一方のボードで友達を削除すると、近くにいるもう一方のボードにはすぐ通知されます。オフラインの場合は、次に遭遇が記録されたときに削除が反映されます。友達が範囲内にいる間、描画が切り替わります。"),
    "Friendship requests expire after five minutes and disappear when the board restarts. Keep both boards nearby with Bluetooth enabled to accept a request.": ("Freundschaftsanfragen laufen nach fünf Minuten ab und verschwinden beim Neustart des Geräts. Halte beide Geräte mit aktiviertem Bluetooth in der Nähe, um eine Anfrage anzunehmen.", "フレンド申請は5分で期限切れになり、ボードの再起動時に消えます。申請を承認するには、両方のボードをBluetooth有効のまま近くに置いてください。"),
    "Friendship requests expire after five minutes and are kept in volatile memory. The board limits request and nearby tracking to 32 entries, so requests do not renew indefinitely or grow memory use. An unavailable board or older firmware does not cause a friend record to be deleted.": ("Freundschaftsanfragen laufen nach fünf Minuten ab und werden nur flüchtig gespeichert. Das Gerät begrenzt Anfragen und Geräte in der Nähe auf 32 Einträge, damit Anfragen nicht unbegrenzt erneuert werden und der Speicherverbrauch nicht wächst. Ein nicht verfügbares Gerät oder ältere Firmware löscht keinen Freundeseintrag.", "フレンド申請は5分で期限切れになり、揮発性メモリに保存されます。ボードは申請と近くのデバイスを32件に制限するため、申請が無期限に更新されたりメモリ使用量が増えたりしません。相手が利用できない場合や古いファームウェアでも友達の記録は削除されません。"),
    "LED power limits": ("LED-Leistungsgrenzen", "LED電力制限"),
    "A ceiling for LED power, not a target brightness. Dim patterns can use less. Rev 1 can be set up to 4.5 W; Rev 2 up to 8 W.": ("Eine Obergrenze für die LED-Leistung, keine Zielhelligkeit. Dunkle Muster verbrauchen weniger. Rev 1 erlaubt bis 4,5 W, Rev 2 bis 8 W.", "LED電力の上限であり、目標の明るさではありません。暗いパターンは消費電力が少なくなります。Rev 1は最大4.5 W、Rev 2は最大8 Wです。"),
    "Only prevents Max LED power from being set lower than this value. It does not force the LEDs to use that much power. Leave it at 0 W unless you need that setting constraint.": ("Verhindert nur, dass die maximale LED-Leistung unter diesen Wert gestellt wird. Die LEDs verbrauchen dadurch nicht automatisch so viel. Falls nicht benötigt, bei 0 W lassen.", "最大LED電力をこの値より低く設定できないようにするだけです。LEDにその電力を使わせるものではありません。必要なければ0 Wのままにしてください。"),
    "On Rev 2, the hotter board-temperature sensor reduces LED power above 50 &deg;C and turns LEDs off at 70 &deg;C. The LEDs also turn off if a sensor reading is unavailable. These safety limits always take priority over your Max LED power setting.": ("Bei Rev 2 reduziert der heißere Temperatursensor die LED-Leistung ab 50 &deg;C und schaltet sie bei 70 &deg;C ab. Ohne gültigen Sensorwert bleiben die LEDs ebenfalls aus. Diese Sicherheitsgrenzen haben immer Vorrang.", "Rev 2では、2つの温度センサーの高い方が50 &deg;Cを超えるとLED電力を下げ、70 &deg;Cで消灯します。センサー値が得られない場合も消灯します。これらの安全制限が常に優先されます。"),
    "Wi-Fi &amp; hotspot": ("WLAN &amp; Hotspot", "Wi-Fiとホットスポット"),
    "Save up to two networks. The controller tries WiFi 1 first, then WiFi 2. Use": ("Speichere bis zu zwei Netzwerke. Der Controller versucht zuerst WiFi 1, dann WiFi 2. Mit", "ネットワークを2つまで保存できます。コントローラーはWiFi 1、次にWiFi 2を試します。"),
    "to choose a network. Restart after saving a profile change to use it.": ("wählst du ein Netzwerk. Nach einer Profiländerung neu starten.", "でネットワークを選びます。プロファイル変更後は再起動してください。"),
    "If neither saved network connects, the controller starts its own Wi-Fi hotspot. Connect to it and open": ("Wenn keines der gespeicherten Netzwerke verbindet, startet der Controller einen eigenen WLAN-Hotspot. Verbinde dich damit und öffne", "保存済みのネットワークに接続できないと、コントローラーは独自のWi-Fiホットスポットを起動します。接続してブラウザーで"),
    "in your browser. Set its name and password here; leave the password box empty to keep the existing password.": ("im Browser. Hier kannst du Name und Passwort festlegen; ein leeres Passwortfeld behält das bisherige Passwort.", "を開いてください。ここで名前とパスワードを設定します。パスワード欄を空にすると現在のパスワードが維持されます。"),
    "Status &amp; maintenance": ("Status &amp; Wartung", "状態とメンテナンス"),
    "Shows live temperatures and estimated LED power. Hardware LED limit is the fixed ceiling for your board revision; Rev 2 temperature protection may lower the actual limit.": ("Zeigt aktuelle Temperaturen und geschätzte LED-Leistung. Die Hardware-LED-Grenze ist die feste Obergrenze der Platine; der Temperaturschutz von Rev 2 kann die tatsächliche Grenze senken.", "現在の温度と推定LED電力を表示します。ハードウェアLED上限は基板リビジョンの固定上限で、Rev 2の温度保護により実際の上限が下がる場合があります。"),
    "The clock syncs from the internet when Wi-Fi is connected. If it has not synced, use": ("Bei WLAN-Verbindung synchronisiert sich die Uhr über das Internet. Falls noch nicht synchronisiert, verwende", "Wi-Fi接続時にインターネットから時刻を同期します。まだ同期していない場合は"),
    "so encounter times can be recorded.": ("damit Begegnungszeiten gespeichert werden können.", "を使うと遭遇時刻を記録できます。"),
    "Shows saved data and free space. You can export people and encounters before resetting that history.": ("Zeigt gespeicherte Daten und freien Platz. Vor dem Zurücksetzen kannst du Personen und Begegnungen exportieren.", "保存データと空き容量を表示します。履歴をリセットする前に、ユーザーと遭遇履歴をエクスポートできます。"),
    "Firmware update": ("Firmware-Update", "ファームウェア更新"),
    "Install a compatible firmware file here. Saved settings and Draw pages are kept; the controller restarts when the update finishes.": ("Hier kannst du eine kompatible Firmware-Datei installieren. Einstellungen und Zeichenseiten bleiben erhalten; danach startet der Controller neu.", "互換性のあるファームウェアファイルをインストールします。設定と描画ページは保持され、更新後にコントローラーが再起動します。"),
    "No matches": ("Keine Treffer", "一致なし"),
    "Selected": ("Ausgewählt", "選択済み"),
    "Use": ("Verwenden", "使用"),
    "Unknown": ("Unbekannt", "不明"),
    "Move up": ("Nach oben", "上へ"),
    "Move down": ("Nach unten", "下へ"),
    "Remove": ("Entfernen", "削除"),
    "Controller UTC time is not synchronized.": ("Die UTC-Zeit des Controllers ist nicht synchronisiert.", "コントローラーのUTC時刻が同期されていません。"),
    "Controller time differs from this browser by more than one minute.": ("Die Controllerzeit weicht um mehr als eine Minute vom Browser ab.", "コントローラーの時刻がブラウザーと1分以上ずれています。"),
    "Time synchronization failed.": ("Zeitsynchronisierung fehlgeschlagen.", "時刻の同期に失敗しました。"),
    "Interval must be between 0.1 and 3600 seconds.": ("Das Intervall muss zwischen 0,1 und 3600 Sekunden liegen.", "間隔は0.1～3600秒にしてください。"),
    "Add at least one glyph.": ("Füge mindestens eine Glyphe hinzu.", "グリフを1つ以上追加してください。"),
    "Fade duration must be an integer between 100 and 60000 milliseconds.": ("Die Überblenddauer muss eine ganze Zahl zwischen 100 und 60000 Millisekunden sein.", "フェード時間は100～60000ミリ秒の整数にしてください。"),
    "Live preview active. Press Save to keep it.": ("Live-Vorschau aktiv. Zum Behalten speichern.", "プレビュー中です。保存すると反映されます。"),
    "Live preview failed.": ("Live-Vorschau fehlgeschlagen.", "プレビューに失敗しました。"),
    "Draw through at least two nodes": ("Mindestens zwei Punkte verbinden", "2つ以上の点をなぞってください"),
    "Save failed": ("Speichern fehlgeschlagen", "保存に失敗"),
    "Save failed.": ("Speichern fehlgeschlagen.", "保存に失敗しました。"),
    "Mode and glyph IDs saved.": ("Modus und Glyphen gespeichert.", "モードとグリフIDを保存しました。"),
    "Could not save display settings.": ("Anzeigeeinstellungen konnten nicht gespeichert werden.", "表示設定を保存できませんでした。"),
    "Could not load glyph or sequence data.": ("Glyphen- oder Folgendaten konnten nicht geladen werden.", "グリフまたはシーケンスのデータを読み込めませんでした。"),
    "Load failed": ("Laden fehlgeschlagen", "読み込みに失敗"),
    "Could not load saved draw pages.": ("Zeichenseiten konnten nicht geladen werden.", "保存済みの描画ページを読み込めませんでした。"),
    "Unsaved changes.": ("Nicht gespeicherte Änderungen.", "未保存の変更があります。"),
    "Eraser on": ("Radierer an", "消しゴム：オン"),
    "Maximum of 10 pages.": ("Maximal 10 Seiten.", "ページは最大10個です。"),
    "Page name (maximum 64 UTF-8 bytes)": ("Seitenname (maximal 64 UTF-8-Bytes)", "ページ名（UTF-8で最大64バイト）"),
    "Page names must contain 1 to 64 bytes.": ("Seitennamen müssen 1 bis 64 Bytes lang sein.", "ページ名は1～64バイトにしてください。"),
    "At least one draw page is required.": ("Mindestens eine Zeichenseite ist nötig.", "描画ページが1つ以上必要です。"),
    "Draw pages saved.": ("Zeichenseiten gespeichert.", "描画ページを保存しました。"),
    "Time unavailable": ("Zeit nicht verfügbar", "時刻を取得できません"),
    "Very Near": ("Sehr nah", "とても近い"),
    "Near": ("Nah", "近い"),
    "Far": ("Entfernt", "遠い"),
    "Unknown agent": ("Unbekannter Agent", "不明なエージェント"),
    "Emergency": ("Notfall", "緊急"),
    "Remove friend": ("Freund entfernen", "友達を削除"),
    "Add friend": ("Freund hinzufügen", "友達に追加"),
    "Send Friendship Request": ("Freundschaftsanfrage senden", "フレンド申請を送る"),
    "Accept Request": ("Anfrage annehmen", "申請を承認"),
    "Request pending": ("Anfrage ausstehend", "申請保留中"),
    "Pairing...": ("Kopplung läuft ...", "ペアリング中…"),
    "Cancel request": ("Anfrage abbrechen", "申請をキャンセル"),
    "Could not update friend.": ("Freund konnte nicht aktualisiert werden.", "友達の変更に失敗しました。"),
    "Bluetooth broadcasting and scanning are active.": ("Bluetooth-Senden und -Suche sind aktiv.", "Bluetoothの送信とスキャンが有効です。"),
    "Bluetooth is active; sync UTC time to record encounters.": ("Bluetooth ist aktiv; synchronisiere die UTC-Zeit für Begegnungen.", "Bluetoothは有効です。遭遇を記録するにはUTC時刻を同期してください。"),
    "Bluetooth proximity is disabled in Settings.": ("Bluetooth-Näherung ist in den Einstellungen deaktiviert.", "設定でBluetooth近接機能が無効です。"),
    "Could not load nearby agents.": ("Agenten in der Nähe konnten nicht geladen werden.", "近くのエージェントを読み込めませんでした。"),
    "No matching known agents.": ("Keine passenden bekannten Agenten.", "一致する既知のエージェントはいません。"),
    "Search failed.": ("Suche fehlgeschlagen.", "検索に失敗しました。"),
    "Unavailable": ("Nicht verfügbar", "取得不可"),
    "Not fitted": ("Nicht vorhanden", "未搭載"),
    "HW Limited": ("HW-begrenzt", "HW制限"),
    "SW Limited": ("SW-begrenzt", "SW制限"),
    "Not Limited": ("Nicht begrenzt", "制限なし"),
    "Scanning...": ("Suche läuft ...", "スキャン中…"),
    "No networks found.": ("Keine Netzwerke gefunden.", "ネットワークが見つかりません。"),
    "Wi-Fi scan failed.": ("WLAN-Suche fehlgeschlagen.", "Wi-Fiの検索に失敗しました。"),
    "secure": ("gesichert", "保護あり"),
    "open": ("offen", "オープン"),
    "Username must be at most 64 UTF-8 bytes.": ("Der Benutzername darf höchstens 64 UTF-8-Bytes haben.", "ユーザー名はUTF-8で64バイト以内にしてください。"),
    "Hotspot name is required and a new password must contain at least 8 characters.": ("Hotspot-Name ist erforderlich; ein neues Passwort braucht mindestens 8 Zeichen.", "ホットスポット名は必須です。新しいパスワードは8文字以上にしてください。"),
    "Settings saved. Controller is restarting.": ("Einstellungen gespeichert. Controller startet neu.", "設定を保存しました。コントローラーを再起動しています。"),
    "Settings saved.": ("Einstellungen gespeichert.", "設定を保存しました。"),
    "Settings could not be saved.": ("Einstellungen konnten nicht gespeichert werden.", "設定を保存できませんでした。"),
    "Controller UTC synchronized from browser.": ("Controller-UTC-Zeit vom Browser übernommen.", "ブラウザーからコントローラーのUTC時刻を同期しました。"),
    "Permanently delete all known users and encounter history? Export them first if needed.": ("Alle bekannten Benutzer und Begegnungen endgültig löschen? Bei Bedarf zuerst exportieren.", "既知のユーザーと遭遇履歴を完全に削除しますか？必要なら先にエクスポートしてください。"),
    "Known users and encounter history reset.": ("Benutzer und Begegnungsverlauf zurückgesetzt.", "ユーザーと遭遇履歴をリセットしました。"),
    "Could not reset proximity data.": ("Näherungsdaten konnten nicht zurückgesetzt werden.", "近接データをリセットできませんでした。"),
    "Controller is restarting. Reconnect shortly.": ("Controller startet neu. Gleich wieder verbinden.", "コントローラーを再起動しています。しばらくしてから再接続してください。"),
    "Restart request failed.": ("Neustart fehlgeschlagen.", "再起動の要求に失敗しました。"),
    "Uploading firmware... Keep this page open.": ("Firmware wird hochgeladen ... Seite offen lassen.", "ファームウェアをアップロード中…このページを閉じないでください。"),
    "Update installed. This page will refresh automatically.": ("Update installiert. Diese Seite wird automatisch neu geladen.", "更新をインストールしました。このページは自動的に再読み込みされます。"),
    "Controller did not reconnect. Reload this page when it is available.": ("Der Controller hat sich nicht wieder verbunden. Lade diese Seite neu, sobald er erreichbar ist.", "コントローラーに再接続できませんでした。利用可能になったらページを再読み込みしてください。"),
    "Firmware update failed.": ("Firmware-Update fehlgeschlagen.", "ファームウェア更新に失敗しました。"),
    "Connection lost before the update completed.": ("Verbindung vor Abschluss des Updates unterbrochen.", "更新完了前に接続が切れました。"),
    "Turn on": ("Einschalten", "点灯"),
    "Turn off": ("Ausschalten", "消灯"),
    "Neutral": ("Neutral", "中間"),
    "Cool": ("Kalt", "寒色"),
    "Warm": ("Warm", "暖色"),
    "NW only": ("Nur Neutralweiß", "昼白色のみ"),
    "Unknown Agent": ("Unbekannter Agent", "不明なエージェント"),
    "Flashlight update failed.": ("Taschenlampe konnte nicht aktualisiert werden.", "ライトの更新に失敗しました。"),
}


UNCHANGED_TEXT = {
    "RGB", "0%", "100%", "Ingress Glyph", "Alex", "alex",
    "192.168.4.1", "&mdash;", "v&mdash;", "0&deg;", "180&deg;",
    "Resistance", "Enlightened", "Rev 1", "Rev 2", "WiFi 1", "WiFi 2",
    "firmware.bin",
}


# Fragments inside template literals or generated HTML cannot be handled as
# complete quoted literals. These replacements are confined to script content.
JS_FRAGMENTS = {
    "Showing ${shown.length} of ${all.length}; refine the filters to see more.": ("${shown.length} von ${all.length}; Filter verfeinern, um mehr zu sehen.", "${all.length}件中${shown.length}件を表示。絞り込むとさらに表示できます。"),
    "${all.length} sequence${all.length===1?'':'s'}": ("${all.length} Folge${all.length===1?'':'n'}", "${all.length}件のシーケンス"),
    "No matching known sequences.": ("Keine passenden bekannten Folgen.", "一致する既知のシーケンスはありません。"),
    "No glyphs selected.": ("Keine Glyphen ausgewählt.", "グリフが選択されていません。"),
    "Page ${pages.length}": ("Seite ${pages.length}", "ページ ${pages.length}"),
    "Page ${i+1}": ("Seite ${i+1}", "ページ ${i+1}"),
    "Page 1": ("Seite 1", "ページ 1"),
    "Delete \"${names[pi]}\"? This takes effect when you save.": ("\"${names[pi]}\" löschen? Wirksam nach dem Speichern.", "\"${names[pi]}\"を削除しますか？保存すると反映されます。"),
    "Hello ${status.username}": ("Hallo ${status.username}", "こんにちは、${status.username}"),
    "${p.lastReceivedAgo}s ago": ("vor ${p.lastReceivedAgo} s", "${p.lastReceivedAgo}秒前"),
    "Last: ${when(p.lastSeen)}": ("Zuletzt: ${when(p.lastSeen)}", "最終：${when(p.lastSeen)}"),
    "Total: ${duration(p.totalSeconds)}": ("Gesamt: ${duration(p.totalSeconds)}", "合計：${duration(p.totalSeconds)}"),
    "No agents received in the last ten minutes.": ("In den letzten zehn Minuten keine Agenten empfangen.", "過去10分間に受信したエージェントはいません。"),
    "class=\"emergency-note\">Emergency</b>": ("class=\"emergency-note\">Notfall</b>", "class=\"emergency-note\">緊急</b>"),
    "No matching known agents.": ("Keine passenden bekannten Agenten.", "一致する既知のエージェントはいません。"),
    "Search failed.": ("Suche fehlgeschlagen.", "検索に失敗しました。"),
    "No networks found.": ("Keine Netzwerke gefunden.", "ネットワークが見つかりません。"),
    "Wi-Fi scan failed.": ("WLAN-Suche fehlgeschlagen.", "Wi-Fiの検索に失敗しました。"),
    "Scanning...": ("Suche läuft ...", "スキャン中…"),
    "WiFi ${active+1} is not configured.": ("WiFi ${active+1} ist nicht eingerichtet.", "WiFi ${active+1}は未設定です。"),
    "Controller UTC: ${formattedUtc(s.unixTime)}": ("Controller-UTC: ${formattedUtc(s.unixTime)}", "コントローラーUTC：${formattedUtc(s.unixTime)}"),
    "${Math.abs(delta)}s ${delta<0?'behind':'ahead'}": ("${Math.abs(delta)} s ${delta<0?'zurück':'voraus'}", "${Math.abs(delta)}秒 ${delta<0?'遅れ':'進み'}"),
    "Last NTP synchronization: ${formattedUtc(s.lastNtpSync)}.": ("Letzte NTP-Synchronisierung: ${formattedUtc(s.lastNtpSync)}.", "前回のNTP同期：${formattedUtc(s.lastNtpSync)}。"),
    "NTP is active and waiting for synchronization.": ("NTP ist aktiv und wartet auf die Synchronisierung.", "NTPは有効で、同期を待っています。"),
    "NTP starts automatically after Wi-Fi connects.": ("NTP startet automatisch nach der WLAN-Verbindung.", "Wi-Fiに接続するとNTPが自動的に開始します。"),
    "Update available: ${tag}-${language} - Current ${current}": ("Update verfügbar: ${tag}-${language} – Aktuell ${current}", "更新あり：${tag}-${language} – 現在 ${current}"),
    "${bytes(z.used)} used of ${bytes(z.total)}": ("${bytes(z.used)} von ${bytes(z.total)} belegt", "${bytes(z.total)}中${bytes(z.used)}使用"),
    "Connected to ": ("Verbunden mit ", "接続先："),
    "Password for ": ("Passwort für ", "パスワード："),
    "Watt limits must satisfy 0 <= minimum <= maximum <= ${hardwareMax.toFixed(1)}.": ("Wattgrenzen: 0 <= Minimum <= Maximum <= ${hardwareMax.toFixed(1)}.", "電力制限は 0 <= 最小 <= 最大 <= ${hardwareMax.toFixed(1)} を満たしてください。"),
    "<strong>Flashlight</strong>": ("<strong>Taschenlampe</strong>", "<strong>ライト</strong>"),
    "<label>Brightness <output": ("<label>Helligkeit <output", "<label>明るさ <output"),
    "<label>Color temperature <output": ("<label>Farbtemperatur <output", "<label>色温度 <output"),
    "<output id=\"flashTemperatureValue\">Neutral</output>": ("<output id=\"flashTemperatureValue\">Neutral</output>", "<output id=\"flashTemperatureValue\">中間</output>"),
    "Cool ${50 - state.temperature}": ("Kalt ${50 - state.temperature}", "寒色 ${50 - state.temperature}"),
    "Warm ${state.temperature - 50}": ("Warm ${state.temperature - 50}", "暖色 ${state.temperature - 50}"),
    "<span>Cool</span><span>NW only</span><span>Warm</span>": ("<span>Kalt</span><span>Nur Neutralweiß</span><span>Warm</span>", "<span>寒色</span><span>昼白色のみ</span><span>暖色</span>"),
    "Agent <strong id=\"remoteEmergencyAgent\"": ("Agent <strong id=\"remoteEmergencyAgent\"", "エージェント <strong id=\"remoteEmergencyAgent\""),
    "</strong> has an emergency": ("</strong> hat einen Notfall", "</strong> が緊急事態です"),
    "aria-label=\"Flashlight controls\"": ("aria-label=\"Taschenlampensteuerung\"", "aria-label=\"ライトの操作\""),
    "<button id=\"flashPower\" type=\"button\">Turn on</button>": ("<button id=\"flashPower\" type=\"button\">Einschalten</button>", "<button id=\"flashPower\" type=\"button\">点灯</button>"),
}


def translate_asset(name, source, language, version="dev"):
    if name == "sw.js":
        return source.replace("ingress-glyph-v33", f"ingress-glyph-{version}-{language}")
    if name == "nearby.html":
        source = source.replace("toLocaleString(undefined,", "toLocaleString(document.documentElement.lang,")
    if name == "settings.html":
        source = source.replace("Intl.DateTimeFormat(undefined,", "Intl.DateTimeFormat(document.documentElement.lang,")
    if language == "en":
        return source
    index = {"de": 0, "ja": 1}[language]
    source = source.replace('<html lang="en">', f'<html lang="{language}">')
    if name == "settings.html":
        labels = {key: TEXT[key][index] for key in ("HW Limited", "SW Limited", "Not Limited")}
        mapping = json.dumps(labels, ensure_ascii=False, separators=(",", ":"))
        source = source.replace("s.powerLimitSource", f"({mapping}[s.powerLimitSource]||s.powerLimitSource)")
    if name.endswith(".html"):
        chunks = re.split(r"(<script\b.*?</script>)", source, flags=re.I | re.S)
        for number, chunk in enumerate(chunks):
            if number % 2:
                chunks[number] = translate_js(chunk, index)
            else:
                def replace_text(match):
                    original = match.group(1)
                    stripped = original.strip()
                    if not stripped:
                        return match.group(0)
                    if stripped not in TEXT:
                        if stripped not in UNCHANGED_TEXT:
                            raise ValueError(f"Missing {language} translation in {name}: {stripped!r}")
                        return match.group(0)
                    leading = original[:len(original) - len(original.lstrip())]
                    trailing = original[len(original.rstrip()):]
                    return '>' + leading + TEXT[stripped][index] + trailing + '<'
                chunk = re.sub(r'>([^<>]+)<', replace_text, chunk)
                def replace_attribute(match):
                    label = match.group(2)
                    if label not in TEXT and label not in UNCHANGED_TEXT:
                        raise ValueError(f"Missing {language} translation in {name}: {label!r}")
                    return f'{match.group(1)}="{TEXT[label][index] if label in TEXT else label}"'
                chunk = re.sub(r'(placeholder|aria-label|title)="([^"]+)"', replace_attribute, chunk)
                chunks[number] = chunk
        translated = ''.join(chunks)
        if name == "help.html":
            steps = (
                (
                    '<li>Lege deinen <strong>Benutzernamen</strong> auf der <a href="/settings">Einstellungsseite</a> fest und speichere ihn.</li>',
                    '<li>Füge auf der <a href="/draw">Zeichenseite</a> eine Seite hinzu oder benenne sie <strong>genau wie deinen Benutzernamen</strong>.</li>',
                    '<li>Zeichne dein Motiv und tippe dann auf <strong>Speichern</strong> auf der Zeichenseite.</li>',
                ),
                (
                    '<li>Auf der <a href="/settings">設定ページ</a>で<strong>ユーザー名</strong>を設定して保存します。</li>',
                    '<li><a href="/draw">描画ページ</a>でページを追加するか、ページ名を<strong>ユーザー名と完全に同じ名前</strong>にします。</li>',
                    '<li>デザインを描き、描画ページの<strong>保存</strong>をタップします。</li>',
                ),
            )[index]
            translated = re.sub(
                r'(<ol class="help-steps">).*?(</ol>)',
                lambda m: m.group(1) + '\n      ' + '\n      '.join(steps) + '\n    ' + m.group(2),
                translated, count=1, flags=re.S,
            )
        return translated
    if name == "flashlight.js":
        return translate_js(source, index)
    if name == "manifest.webmanifest":
        manifest = json.loads(source)
        manifest["name"] = ("Ingress-Glyphen-Controller", "Ingress グリフ コントローラー")[index]
        return json.dumps(manifest, ensure_ascii=False)
    return source


def translate_js(source, index):
    for english, translations in sorted(JS_FRAGMENTS.items(), key=lambda item: -len(item[0])):
        source = source.replace(english, translations[index])
    for english, translations in TEXT.items():
        if "'" in english or "'" in translations[index]:
            continue
        source = source.replace("'" + english + "'", "'" + translations[index] + "'")
    return source
