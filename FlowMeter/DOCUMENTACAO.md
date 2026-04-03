# FlowMeter -- Documentação Completa do Sistema

## 1. Visão Geral

Sistema embarcado para medição e controle de fluxo de líquidos (chopeiras), baseado em **ESP8266** (NodeMCU / Wemos D1 Mini). Controla uma **válvula solenóide**, mede o volume dispensado através de um **sensor de fluxo Hall**, exibe informações num **display OLED SSD1306** e comunica-se via **MQTT** e **HTTP**.

O firmware é **white-label**: todas as configurações de marca, broker, tópicos e calibração são persistidas em EEPROM e editáveis via **WebServer embarcado** (HTTP na porta 80, protegido por **autenticação Basic** com credenciais definidas no firmware), sem necessidade de recompilar para alterar MQTT/EEPROM — apenas para mudar utilizador/senha do painel web.

---

## 2. Hardware

### 2.1 Pinagem

| Pino ESP8266 | Função | Componente |
|---|---|---|
| **D1** | Saída digital | Válvula solenóide (via relé/MOSFET). HIGH = fechada, LOW = aberta |
| **D2** | Entrada com pull-up interno | Sensor de fluxo Hall (pulsos em borda de descida) |
| **D3** (SDA) | I2C Data | Display OLED SSD1306 |
| **D5** (SCL) | I2C Clock | Display OLED SSD1306 |

### 2.2 Componentes

- **ESP8266** (NodeMCU ou Wemos D1 Mini)
- **Display OLED** 128x64, SSD1306, endereço I2C `0x3C`
- **Sensor de fluxo** tipo Hall (ex: YF-S201 ou similar)
- **Válvula solenóide** (12V ou 24V, acionada via relé ou MOSFET)
- **Relé ou MOSFET** para acionar a solenóide a partir do pino D1 (3.3V lógico)

### 2.3 Alimentação

- **ESP8266**: 5V via USB ou Vin (regulador interno gera 3.3V)
- **Sensor de fluxo**: recomendado **5V** no VCC do sensor para pulsos mais limpos. Se alimentado a 5V, o sinal de saída deve ser limitado a 3.3V no GPIO (divisor de tensão ou pull-up a 3.3V em saída open-drain)
- **Válvula solenóide**: alimentação separada (12V/24V), acionada pelo relé/MOSFET. GND comum com o ESP

---

## 3. Arquitectura de Software

### 3.1 Ficheiros do Projecto

```
FlowMeter/
├── FlowMeter.ino      -- Setup, loop principal, variáveis globais
├── Config.h / .cpp    -- Struct DeviceConfig, persistência EEPROM, flag de portal WiFi (`WP`)
├── Display.h / .cpp   -- Controle do OLED SSD1306 via I2C (Wire)
├── FlowMeter.h / .cpp -- Contagem de pulsos, cálculo de volume, lógica de display
├── Mqtt.h / .cpp      -- Conexão MQTT, callback de mensagens, validação de token
├── Http.h / .cpp      -- Relatório de consumo via HTTP POST (webhook)
├── WebServer.h / .cpp -- Servidor HTTP (porta 80), Basic Auth, formulário e `POST /wifi-portal`
└── Fonts.h            -- Fontes customizadas para o display
```

### 3.2 Diagrama de Fluxo

```
                    ┌──────────────┐
                    │   EEPROM     │
                    │ DeviceConfig │
                    └──────┬───────┘
                           │ loadConfig()
                           ▼
┌─────────┐  WiFi   ┌─────────────┐  MQTT    ┌────────────┐
│WiFiMgr  │────────▶│  ESP8266    │◀────────▶│ Broker MQTT│
│  (AP)   │         │  (loop)     │          └────────────┘
└─────────┘         │             │
                    │  ┌────────┐ │  HTTP     ┌────────────┐
                    │  │WebSrv  │─┼─────────▶│  Webhook   │
                    │  └────────┘ │  POST     │  Backend   │
                    │             │           └────────────┘
                    │  ┌────────┐ │
                    │  │Display │ │  I2C ──▶ OLED SSD1306
                    │  └────────┘ │
                    │             │
                    │  D1 ───────▶ Válvula Solenóide
                    │  D2 ◀─────── Sensor de Fluxo
                    └─────────────┘
```

### 3.3 Sequência de Boot

1. Configura pinos (D1 como saída HIGH = válvula fechada, D2 como entrada pull-up)
2. Inicializa EEPROM (512 bytes) e Serial (115200 baud)
3. Imprime motivo do último reset (`ESP.getResetReason()`)
4. Carrega configuração da EEPROM (`loadConfig()`). Se inválida, carrega defaults e salva
5. Inicializa display OLED
6. **Portal WiFi pendente (opcional):** se na EEPROM estiver activa a [flag de portal WiFi](#43-flag-de-portal-wifi-eeprom-fora-do-deviceconfig) (bytes 500–501 = `WP`), o firmware:
   - Mostra no display: *Portal WiFi*, linha com o SSID do AP (`FLOWMETER_WIFI_AP_NAME`, ex.: *BorgattoTapMeter*), *192.168.4.1*
   - Chama `WiFiManager.startConfigPortal(FLOWMETER_WIFI_AP_NAME, FLOWMETER_WIFI_AP_PASSWORD)` com **timeout de 300 segundos** (5 minutos)
   - Ao sair do portal (utilizador gravou nova rede, ou timeout), **limpa a flag** na EEPROM, executa `ESP.restart()` e continua o arranque normal
7. **WiFi em modo normal:** se não há portal pendente, conecta via `WiFiManager.autoConnect` com o mesmo SSID/senha do AP de configuração (o AP só fica activo durante este processo até haver credenciais válidas ou timeout conforme WiFiManager)
8. Aguarda `WL_CONNECTED` e mostra SSID + IP no display
9. **Diagnóstico no Serial:** imprime `WiFi.localIP()`, activa `WiFi.setSleepMode(WIFI_NONE_SLEEP)` (evita sono WiFi que pode atrasar respostas), e imprime **MAC**, **gateway** (`WiFi.gatewayIP()`) e **máscara de sub-rede** (`WiFi.subnetMask()`) — útil para confirmar que o PC/browser está na mesma sub-rede roteável até ao ESP
10. Conecta ao broker MQTT com dados da configuração
11. Inicia WebServer (porta **80**)
12. Ativa interrupção no pino do sensor (borda de descida)
13. Exibe tela de "Aguardando Liberação"

### 3.4 Loop Principal

```
loop() {
  1. mqtt.loop()                -- processa mensagens MQTT
  2. Verifica mqttUiPending     -- atualiza display se houve comando MQTT
  3. webserver.handleClient()   -- atende requisições HTTP
  4. Verifica httpReportPending -- envia relatório de consumo se pendente
  5. Verifica valveStabilizing  -- se em debounce, aguarda e depois zera contadores
  6. flowMeter.calculateFlowV1()-- calcula volume, atualiza display, verifica limites
}
```

---

## 4. Configuração do Dispositivo (EEPROM)

### 4.1 Struct DeviceConfig

| Campo | Tipo | Tamanho | Descrição |
|---|---|---|---|
| `magic` | char[4] | 4 | Marcador "FLW\0" para validar EEPROM |
| `deviceId` | char[32] | 32 | Identificador único do dispositivo |
| `brandName` | char[32] | 32 | Título da página web de configuração; no OLED só se `oledTitle` estiver vazio |
| `mqttBroker` | char[64] | 64 | Endereço do broker MQTT |
| `mqttPort` | uint16_t | 2 | Porta do broker MQTT |
| `mqttUser` | char[32] | 32 | Usuário MQTT (vazio se sem autenticação) |
| `mqttPass` | char[32] | 32 | Senha MQTT |
| `mqttTopicBase` | char[64] | 64 | Tópico base MQTT. O tópico `/set` é derivado automaticamente |
| `webhookUrl` | char[128] | 128 | URL para POST HTTP de relatórios de consumo |
| `deviceToken` | char[64] | 64 | Token de segurança para filtrar mensagens MQTT |
| `defaultConvFactor` | float | 4 | Fator de conversão padrão (pulsos → mL) |
| `valveDebounceMs` | uint16_t | 2 | Tempo de debounce da solenóide em ms |
| `oledTitle` | char[32] | 32 | Texto da **linha superior do OLED**; se vazio, usa-se `brandName` |

O registo `DeviceConfig` começa no **offset 0** da EEPROM. `EEPROM.put(0, config)` grava a struct completa. A **flag de portal WiFi** e o **marcador de schema** estão em endereços **fixos** altos (fora da struct), para upgrades não corromperem dados — ver [secção 4.3](#43-flag-de-portal-wifi-eeprom-fora-do-deviceconfig).

### 4.2 Valores Padrão (primeiro boot)

| Campo | Valor padrão |
|---|---|
| deviceId | `Device1` |
| brandName | `FlowMeter` |
| oledTitle | *(vazio)* — o display usa então o mesmo texto que `brandName` |
| mqttBroker | `broker.hivemq.com` |
| mqttPort | `1883` |
| mqttTopicBase | `AccesysFlowMeter/Device1` |
| defaultConvFactor | `0.2207` |
| valveDebounceMs | `400` |

### 4.3 Flag de portal WiFi e schema EEPROM (fora do DeviceConfig)

Para **reabrir o portal do WiFiManager** a partir do WebServer e para **migrações de firmware** sem misturar bytes da struct com flags, usam-se endereços fixos (dentro de `EEPROM.begin(512)`):

| Offset absoluto | Conteúdo |
|---|---|
| **500–501** | Flag de portal: `'W'` + `'P'` = pedido pendente; `0` + `0` = inactivo |
| **502** | Marcador de schema `0xC5`: indica que o campo `oledTitle` já foi inicializado neste firmware |

**API (`Config.cpp`):** `setWifiPortalPending()`, `isWifiPortalPending()`, `clearWifiPortalPending()` — leem/gravam os bytes **500–501**.

**Upgrade a partir de firmware antigo:** se o byte **502** não for `0xC5`, o `loadConfig()` assume EEPROM antiga, **zera `oledTitle`**, grava a struct e o marcador, e faz `commit` (evita lixo na linha superior do OLED ao aumentar o tamanho de `DeviceConfig`).

**SSID do AP de configuração WiFi:** definido em `Config.h` como `FLOWMETER_WIFI_AP_NAME` (valor actual: **`BorgattoTapMeter`**) e `FLOWMETER_WIFI_AP_PASSWORD` (**`12345678`**). Alterar o nome da rede de configuração implica recompilar.

---

## 5. WebServer Embarcado

### 5.1 Como aceder

1. **Primeiro boot (sem credenciais WiFi gravadas ou falha de ligação):** o WiFiManager expõe o AP **`BorgattoTapMeter`** (senha `12345678`, macro `FLOWMETER_WIFI_AP_PASSWORD`). Ligar a esse AP e abrir `http://192.168.4.1` para o portal cativo.
2. **Após ligação WiFi em modo STA:** o **IP** aparece no display OLED e no Serial (115200 baud). Num browser **na mesma rede IP** (mesma sub-rede ou com roteamento/firewall que permita TCP 80 até ao ESP), abrir `http://<IP>/`.

**Importante:** todas as rotas listadas na [secção 5.3](#53-endpoints) exigem **autenticação HTTP Basic** (excepto a resposta `401` que pede credenciais). Sem utilizador/senha correctos, o browser não mostra o formulário nem os JSON.

### 5.2 Autenticação HTTP (Basic Auth)

O servidor usa **HTTP Basic Authentication** (suportado nativamente por `ESP8266WebServer::authenticate` / `requestAuthentication`).

| Parâmetro | Valor por defeito (firmware) |
|---|---|
| **Utilizador** | `admin` |
| **Senha** | `ADMBORGATTO` |

Estas constantes estão definidas em `WebServer.cpp` (`kWebAdminUser`, `kWebAdminPass`). Para alterar a senha em produção, é necessário **recompilar** e voltar a gravar o firmware (não estão na EEPROM).

**Fluxo no browser:** ao aceder a `/` ou `/status`, o servidor responde com `401 Unauthorized` e o cabeçalho `WWW-Authenticate: Basic realm="..."`; o browser abre a janela de login. Após sucesso, o browser reenvia o cabeçalho `Authorization: Basic <base64>` em pedidos subsequentes na mesma sessão.

**Limitações de segurança:**

- O tráfego é **HTTP em claro** (porta 80). A senha não vai cifrada na rede local — qualquer equipamento que consiga capturar pacotes na mesma LAN pode ver o Base64 (que é reversível). Trata-se de **obstáculo contra acesso casual**, não de protecção forte.
- Não há HTTPS nativo trivial no ESP8266 para este WebServer; ambientes exigentes devem usar VPN, VLAN de gestão, ou desactivar o servidor após configuração.

### 5.3 Endpoints

| Método | Rota | Autenticação | Descrição |
|---|---|---|---|
| `GET` | `/` | Basic obrigatória | Formulário HTML de configuração com valores actuais + acções de reinício e portal WiFi |
| `POST` | `/config` | Basic obrigatória | Processa o formulário e grava a configuração na EEPROM (`saveConfig`) |
| `GET` | `/status` | Basic obrigatória | Retorna JSON com estado actual do dispositivo |
| `POST` | `/restart` | Basic obrigatória | Responde com página de confirmação, aguarda ~500 ms e executa `ESP.restart()` |
| `POST` | `/wifi-portal` | Basic obrigatória | Grava a [flag de portal WiFi](#43-flag-de-portal-wifi-eeprom-fora-do-deviceconfig) (`WP`), responde com mensagem HTML e reinicia; no próximo boot entra em `startConfigPortal` (ver [secção 5.4](#54-abrir-o-portal-wifimanager-pelo-webserver)) |

Pedidos com método ou rota não tratados pelo `server.on(...)` comportam-se conforme o core (tipicamente 404).

### 5.4 Abrir o portal WiFiManager pelo WebServer

Objectivo: estando o ESP **já ligado** ao WiFi (e conseguires aceder a `http://<IP>/` com autenticação), **forçar um reinício em modo configuração** sem apagar credenciais manualmente até o utilizador escolher nova rede no portal.

**Passos:**

1. Autenticar no WebServer e abrir a página principal (`GET /`).
2. Na secção **Rede WiFi**, clicar no botão **Abrir portal WiFi** (formulário com `method="POST"` e `action="/wifi-portal"`).
3. O firmware executa `setWifiPortalPending()`, envia uma página HTML breve (“A reiniciar…”) e chama `ESP.restart()`.
4. No arranque seguinte, `isWifiPortalPending()` é verdadeiro: o código **não** chama `autoConnect` de imediato; entra em `startConfigPortal` com SSID **`BorgattoTapMeter`** e timeout de **300 s**.
5. Ligar o telemóvel ou portátil ao AP **`BorgattoTapMeter`** e abrir `http://192.168.4.1` para configurar SSID/senha como no primeiro uso.
6. Ao terminar (ou ao expirar o timeout), `clearWifiPortalPending()` é chamado e o dispositivo **reinicia** outra vez; em seguida o fluxo normal `autoConnect` + MQTT + WebServer volta a correr.

**Restrição de rede:** se o teu PC **não** consegue aceder ao IP do ESP (VLANs isoladas, *client isolation*, etc.), **também não** consegues usar este botão — o contorno continua a ser política de rede ou ligar directamente ao AP quando o portal estiver activo (primeiro boot ou após este fluxo).

**Implementação:** o portal **não** é embutido na mesma instância `ESP8266WebServer` da configuração; usa-se o mecanismo oficial do **WiFiManager** após reinício, para evitar conflitos de servidores HTTP e bloqueios dentro de handlers.

### 5.5 Resposta do GET /status

O mesmo esquema **Basic Auth** aplica-se: clientes como `curl`, Postman ou monitorização devem enviar o cabeçalho `Authorization` ou usar a opção equivalente.

**Exemplo com `curl` (Windows / Linux):**

```bash
curl -u admin:ADMBORGATTO http://192.168.0.35/status
```

Substituir o IP pelo endereço real do dispositivo.

**Corpo JSON típico:**

```json
{
  "deviceId": "Device1",
  "ip": "192.168.1.100",
  "rssi": -45,
  "uptime": 3600,
  "volumeMl": 250,
  "valor": 2.50,
  "chopp": "California IPA",
  "codCliente": 1,
  "conversionFactor": 0.2207
}
```

### 5.6 Interface HTML (página principal)

Além dos campos de dispositivo, MQTT, webhook e calibração, a página `GET /` inclui:

- **Nome da Marca** — título da página web de configuração (`brandName`)
- **Título no display OLED** — texto da linha de cima do ecrã (`oledTitle`); se deixares vazio, o firmware usa `brandName` no OLED (após **reiniciar** o dispositivo para aplicar)
- **Salvar Configuração** — `POST /config`
- **Reiniciar Dispositivo** — `POST /restart`
- **Rede WiFi** — texto com o SSID do AP de configuração e **Abrir portal WiFi** — `POST /wifi-portal` (ver [secção 5.4](#54-abrir-o-portal-wifimanager-pelo-webserver))

Todos estes envios reutilizam a sessão autenticada do browser (mesmo utilizador/senha Basic Auth).

---

## 6. Protocolo MQTT

### 6.1 Tópicos

O dispositivo subscreve **dois tópicos**, ambos derivados de `config.mqttTopicBase`:

| Tópico | Exemplo | Uso |
|---|---|---|
| `{mqttTopicBase}` | `AccesysFlowMeter/Device1` | Receber comandos e publicar status |
| `{mqttTopicBase}/set` | `AccesysFlowMeter/Device1/set` | Receber comandos de configuração |

### 6.2 Formato da Mensagem JSON (entrada)

```json
{
  "token": "meu-token-secreto",
  "comando": 1,
  "valorMl": 1.00,
  "saldo": 10.0,
  "quantidade": 0,
  "fatorConversao": 0.2207,
  "descricao": "California IPA",
  "codCliente": 1
}
```

### 6.3 Campos da Mensagem

| Campo | Tipo | Obrigatório | Descrição |
|---|---|---|---|
| `token` | string | Condicional | Token de segurança. Se `deviceToken` está configurado na EEPROM, este campo **deve** coincidir. Se `deviceToken` está vazio, qualquer mensagem é aceite |
| `comando` | int | Sim | `1` = abrir válvula (liberar fluxo). `0` = fechar válvula (bloquear fluxo) |
| `valorMl` | double | Sim | Valor cobrado por mL (em centavos). Usado no cálculo: `totalValue = flowMilliLitres * valorMl / 100` |
| `saldo` | double | Sim | Saldo máximo permitido (em R$). Quando `totalValue >= saldo`, a válvula fecha automaticamente |
| `quantidade` | double | Sim | Volume máximo em mL. Se `> 0` e `flowMilliLitres >= quantidade`, a válvula fecha automaticamente. Use `0` para sem limite de volume |
| `fatorConversao` | float | Não | Fator de conversão (pulsos → mL) específico para o chopp actual. Se `0`, ausente ou nulo, mantém o valor do config (`defaultConvFactor`) |
| `descricao` | string | Não | Nome do chopp exibido no display. Se vazio ou ausente, exibe "Chopp" |
| `codCliente` | int | Sim | Código do cliente (incluído no relatório HTTP) |

### 6.4 Exemplos de Uso

**Liberar fluxo com limite por saldo:**

```json
{
  "token": "abc123",
  "comando": 1,
  "valorMl": 0.05,
  "saldo": 10.0,
  "quantidade": 0,
  "fatorConversao": 0.2207,
  "descricao": "Pilsen",
  "codCliente": 42
}
```
Resultado: válvula abre. O display mostra volume e valor em tempo real. Quando o valor atinge R$ 10,00, a válvula fecha automaticamente e o display congela nos valores-alvo.

**Liberar fluxo com limite por volume (100 mL):**

```json
{
  "token": "abc123",
  "comando": 1,
  "valorMl": 1.00,
  "saldo": 999.0,
  "quantidade": 100,
  "fatorConversao": 0.25,
  "descricao": "IPA",
  "codCliente": 7
}
```
Resultado: válvula abre. Quando atinge 100 mL, fecha automaticamente. O display mostra exatamente 100 mL e o valor correspondente.

**Fechar válvula manualmente:**

```json
{
  "token": "abc123",
  "comando": 0,
  "valorMl": 0,
  "saldo": 0,
  "quantidade": 0,
  "fatorConversao": 0,
  "descricao": "",
  "codCliente": 0
}
```

### 6.5 Validação de Token

- Se `config.deviceToken` está **vazio** (não configurado): todas as mensagens são aceites (retrocompatível)
- Se `config.deviceToken` está **preenchido**: o campo `token` do JSON deve coincidir exatamente. Mensagens com token errado ou ausente são silenciosamente ignoradas (log no Serial)

Isso permite que múltiplos dispositivos partilhem o **mesmo tópico MQTT** e cada um reaja apenas às suas mensagens. Útil com AWS IoT ou brokers com gestão manual de tópicos.

---

## 7. Relatório HTTP (Webhook)

### 7.1 Quando é Enviado

O relatório é enviado automaticamente quando:
- O consumo atinge o limite (por saldo ou por quantidade) e o display é congelado
- A flag `httpReportPending` é ativada

### 7.2 Formato do POST

**URL:** configurada em `config.webhookUrl` via WebServer

**Headers:** `Content-Type: application/json`

**Body:**

```json
{
  "deviceId": "Device1",
  "codCliente": 42,
  "volumeMl": 100,
  "valor": 1.00,
  "descricao": "California IPA",
  "uptimeMs": 123456
}
```

### 7.3 Campos do Relatório

| Campo | Tipo | Descrição |
|---|---|---|
| `deviceId` | string | ID do dispositivo (da EEPROM) |
| `codCliente` | int | Código do cliente que consumiu |
| `volumeMl` | unsigned long | Volume dispensado em mL (usa valores congelados se disponíveis) |
| `valor` | double | Valor total cobrado em R$ |
| `descricao` | string | Nome do chopp servido |
| `uptimeMs` | unsigned long | Tempo de atividade do dispositivo em ms |

---

## 8. Display OLED

### 8.1 Telas do Sistema

| Tela | Linha 1 | Linha 2 | Linha 3 | Linha 4 |
|---|---|---|---|---|
| **Boot / Welcome** | Nome da marca | Mensagem | Submensagem | Detalhe |
| **Aguardando** | Nome da marca | Nome do chopp | "Aguardando" | "Liberação" |
| **Liberado** | Nome da marca | Nome do chopp | "Liberado" | "Sirva-se" |
| **Servindo** | Nome da marca | Nome do chopp | "V: XXX ml" | "R$: X.XX" |
| **Concluído** | Nome da marca | "Concluído" | "V: XXX ml" | "R$: X.XX" |

### 8.2 Throttling do Display

O display é atualizado no máximo a cada **150 ms** (configurável em `DISPLAY_INTERVAL_MS`), ou a cada **50 ms** se o valor mudou significativamente. Isso evita sobrecarga do barramento I2C e previne watchdog resets.

---

## 9. Debounce da Válvula Solenóide

### 9.1 Problema

A abertura/fechamento mecânico da solenóide gera vibração que o sensor Hall interpreta como pulsos falsos, resultando em volume "fantasma" no display.

### 9.2 Solução Implementada

1. **Callback MQTT** recebe comando → `detachInterrupt()` (para a contagem) → aciona válvula → define flag `valveStabilizing = true` com timestamp
2. **Loop principal** detecta `valveStabilizing` → ignora pulsos durante `config.valveDebounceMs` (padrão: 400 ms)
3. Após o debounce → **zera todos os contadores** → `attachInterrupt()` (reativa contagem)
4. A partir daqui, o display começa do zero

### 9.3 Ajuste do Debounce

- **300-500 ms**: cobre a maioria das solenóides de chopeira
- **600-800 ms**: se ainda houver pulsos espúrios
- **200 ms**: se a resposta precisa ser mais rápida
- Configurável via WebServer no campo "Debounce da Válvula (ms)"

---

## 10. Congelamento do Display no Limite

### 10.1 Comportamento

Quando o volume/valor atinge o limite (saldo ou quantidade), o display **congela** nos valores teóricos do limite (não nos valores reais do sensor). Isso evita que o líquido residual na tubagem (entre a válvula e o sensor) inflacione os números exibidos.

Exemplo: limite de saldo = R$ 1,00 com valorMl = 1,00
- Display congela em: **V: 100 ml** e **R$: 1.00**
- Mesmo que o sensor continue a contar pulsos do líquido residual

### 10.2 Cálculo dos Valores Congelados

**Por saldo:**
- `frozenServingMl = round(saldo * 100 / valorMl)`
- `frozenServingValue = saldo`

**Por quantidade:**
- `frozenServingMl = round(quantidade)`
- `frozenServingValue = quantidade * valorMl / 100`

---

## 11. Bibliotecas Necessárias (Arduino IDE)

| Biblioteca | Uso |
|---|---|
| **ESP8266WiFi** | Conexão WiFi (incluída no core ESP8266) |
| **ESP8266WebServer** | WebServer embarcado (incluída no core) |
| **ESP8266HTTPClient** | Cliente HTTP para webhook (incluída no core) |
| **WiFiManager** | Portal cativo para configuração WiFi |
| **PubSubClient** | Cliente MQTT |
| **ArduinoJson** (v6 ou v7) | Parse/serialização JSON |
| **ESP8266_SSD1306** (ThingPulse) | Driver do display OLED |
| **Wire** | Comunicação I2C (incluída no core) |

### 11.1 Configuração do Arduino IDE

1. **Gestor de Placas:** instalar "ESP8266 by ESP8266 Community"
2. **Placa:** selecionar "NodeMCU 1.0" ou "LOLIN(WEMOS) D1 mini"
3. **Upload Speed:** recomendado 115200 (reduzir se houver erros de upload)
4. **Flash Size:** 4MB (FS:1MB)

---

## 12. Fator de Conversão por Chopp

Cada tipo de chopp pode ter viscosidade e carbonatação diferentes, o que afeta a resposta do sensor de fluxo. O sistema suporta isto de duas formas:

1. **Fator padrão (EEPROM):** `config.defaultConvFactor` -- usado quando nenhum valor é enviado via MQTT. Configurável pelo WebServer
2. **Fator por servida (MQTT):** campo `fatorConversao` no JSON -- sobrescreve temporariamente o fator para aquela servida específica. Se vier como `0` ou ausente, mantém o valor actual

### 12.1 Como Calibrar

1. Definir um volume conhecido (ex: 1000 mL)
2. Enviar comando MQTT com `fatorConversao` estimado
3. Dispensar o volume medido e observar o que o sistema reporta
4. Ajustar: `novoFator = fatorAtual * (volumeReal / volumeReportado)`
5. Salvar o valor ajustado como `defaultConvFactor` no WebServer

---

## 13. Segurança e Considerações

### 13.1 Token MQTT

O token **não é criptografado** -- é comparação de texto plano. Para produção com requisitos de segurança mais elevados, considerar:
- MQTT sobre TLS (porta 8883)
- Autenticação por certificado (ex: AWS IoT)
- Token rotativo via backend

### 13.2 WebServer

O WebServer implementa **HTTP Basic Authentication** em todas as rotas activas (`/`, `/config`, `/status`, `/restart`, `/wifi-portal`). O utilizador e a senha por defeito estão em `WebServer.cpp` (`admin` / `ADMBORGATTO`).

**Riscos residuais:**

- **Rede:** qualquer cliente que consiga abrir uma sessão TCP à porta 80 do ESP (mesma LAN ou rota IP permitida) pode tentar força bruta ou reutilizar credenciais capturadas — não substitui firewall ou segmentação VLAN.
- **Confidencialidade:** sem HTTPS, credenciais e formulários circulam em claro; ver também [secção 5.2](#52-autenticação-http-basic-auth).

**Boas práticas em produção:**

- Alterar a senha no código e manter o binário sob controlo de versões
- Restringir acesso à sub-rede ou IP de gestão no router/firewall
- Considerar desactivar ou não expor o WebServer após a configuração inicial, se a política de segurança o exigir

A rota **`POST /wifi-portal`** é particularmente sensível: permite forçar o dispositivo a entrar em modo de reconfiguração WiFi; mantém a mesma protecção Basic Auth que as restantes rotas.

### 13.3 EEPROM

A EEPROM do ESP8266 suporta ~100.000 ciclos de escrita. Evitar salvar configuração em loop. O sistema só escreve `DeviceConfig` quando o utilizador clica **Salvar Configuração** no WebServer. A **flag de portal WiFi** (`WP`) só é escrita ao confirmar **Abrir portal WiFi** e é apagada após o `startConfigPortal` terminar.

---

## 14. Troubleshooting

| Sintoma | Causa provável | Solução |
|---|---|---|
| Display mostra volume ao ligar | Pulsos espúrios do sensor | Verificar conexão do sensor, aumentar `valveDebounceMs` |
| Reset frequente durante fluxo | Display I2C sobrecarrega WDT | Já mitigado pelo throttling (150 ms). Verificar `ESP.getResetReason()` no Serial |
| Volume exibido > volume real | `fatorConversao` muito alto | Recalibrar (seção 12.1) |
| Display mostra 107 ml em vez de 100 ml | Líquido residual na tubagem | Sistema congela nos valores-alvo (seção 10). Verificar se `saldo` ou `quantidade` estão corretos |
| MQTT não conecta | Broker/porta/credenciais errados | Verificar via WebServer, conferir no Serial Monitor |
| Mensagens MQTT ignoradas | Token não confere | Verificar `deviceToken` no WebServer e `token` no JSON |
| Upload falha (PermissionError COM) | Porta serial em uso | Fechar Serial Monitor e outros programas que usem a porta COM |
| Erro `brzo_i2c` na compilação | Biblioteca Brzo I2C incompatível | Usar `SSD1306Wire` em vez de `SSD1306Brzo` (já corrigido) |
| Browser mostra pedido de login ou página em branco ao abrir o IP | **Basic Auth** activo | Utilizador `admin`, senha `ADMBORGATTO` (ou as que definiste no firmware); ver [secção 5.2](#52-autenticação-http-basic-auth) |
| `401 Unauthorized` ou `curl` sem dados em `/status` | Falta cabeçalho `Authorization` | Usar `curl -u admin:ADMBORGATTO http://<IP>/status` — [secção 5.5](#55-resposta-do-get-status) |
| MQTT funciona mas não há ping nem HTTP ao IP do display | Isolamento entre clientes WiFi, VLANs ou firewall | Ver [secção 5.1](#51-como-aceder); ajustar router (ACL, desactivar *AP isolation*) ou testar a partir da mesma sub-rede |
| Após "Abrir portal WiFi" o equipamento não cria o AP `BorgattoTapMeter` | Reinício falhou ou EEPROM não gravou | Verificar alimentação; no Serial confirmar arranque; confirmar que `POST /wifi-portal` foi enviado com sucesso (após login); procurar a rede pelo SSID actual em `Config.h` |
| Portal WiFi abre mas timeout aos 5 min | `setConfigPortalTimeout(300)` | Completar a configuração no portal ou voltar a usar **Abrir portal WiFi** no WebServer |
| Serial mostra gateway `0.0.0.0` ou estranho | WiFi sem DHCP correcto ou ainda a associar | Conferir router; comparar MAC/gateway/máscara impressos no Serial ([secção 3.3](#33-sequência-de-boot)) com o PC |
