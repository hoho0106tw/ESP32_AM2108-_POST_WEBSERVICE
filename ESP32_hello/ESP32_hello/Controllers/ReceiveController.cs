using System.Globalization;
using System.Text.Json.Serialization;
using Microsoft.AspNetCore.Mvc;

namespace ESP32_hello.Controllers;

// 1. 將變數宣告改為符合 ESP32 傳遞的正確型別 (數字改用 double)
public class SensorData
{
    [JsonPropertyName("temp")]
    public double? Temp { get; set; }

    [JsonPropertyName("humid")]
    public double? Humid { get; set; }

    [JsonPropertyName("time")]
    public string? Time { get; set; }

    [JsonPropertyName("msg")]
    public string? Msg { get; set; }
}

[ApiController]
public class ReceiveController : ControllerBase
{
    private readonly ILogger<ReceiveController> _logger;

    public ReceiveController(ILogger<ReceiveController> logger)
    {
        _logger = logger;
    }

    // 支援網頁瀏覽器直接輸入網址查看環境是否正常
    [HttpGet("/")]
    [HttpGet("/api/receive")]
    public ContentResult DefaultHello()
    {
        var taiwanTime = DateTimeOffset.UtcNow.ToOffset(TimeSpan.FromHours(8));
        var timestamp = taiwanTime.ToString("yyyy-MM-dd HH:mm:ss", CultureInfo.InvariantCulture);
        return Content($"HELLO {timestamp} (Please use POST with JSON for sensor data)", "text/plain; charset=utf-8");
    }

    // 2. 將參數名稱從 data 改為 sensorInput，避開 ASP.NET Core 保留字的驗證衝突
    [HttpPost("/api/receive")]
    [ResponseCache(NoStore = true, Location = ResponseCacheLocation.None)]
    public ContentResult ProcessData([FromBody] SensorData sensorInput)
    {
        var taiwanTime = DateTimeOffset.UtcNow.ToOffset(TimeSpan.FromHours(8));
        var timestamp = taiwanTime.ToString("yyyy-MM-dd HH:mm:ss", CultureInfo.InvariantCulture);

        // 明確通知硬體處理完後關閉此 TCP 連線，防止 ESP32 斷線變 -1
        Response.Headers.Connection = "close";

        if (sensorInput != null && sensorInput.Temp.HasValue && sensorInput.Humid.HasValue)
        {
            // 將數字格式化為保留兩位小數的字串印出
            string tStr = sensorInput.Temp.Value.ToString("F2", CultureInfo.InvariantCulture);
            string hStr = sensorInput.Humid.Value.ToString("F2", CultureInfo.InvariantCulture);

            string logMessage = $"[Server Time: {timestamp}] RECEIVED -> Temp: {tStr}°C, Humid: {hStr}%, Device Time: {sensorInput.Time}";

            _logger.LogInformation(
                "ESP32 data received via JSON: Temp={Temperature}, Humid={Humidity}, DeviceTime={DeviceTime}",
                tStr, hStr, sensorInput.Time);

            return Content(logMessage, "text/plain; charset=utf-8");
        }

        if (sensorInput != null && !string.IsNullOrEmpty(sensorInput.Msg))
        {
            _logger.LogInformation("ESP32 message received via JSON: Message={Message}, DeviceTime={DeviceTime}", sensorInput.Msg, sensorInput.Time);
            return Content($"[Server Time: {timestamp}] RECEIVED -> Message: {sensorInput.Msg}, Device Time: {sensorInput.Time}", "text/plain; charset=utf-8");
        }

        return Content($"[Server Time: {timestamp}] Error: No valid JSON data received.", "text/plain; charset=utf-8");
    }
}
