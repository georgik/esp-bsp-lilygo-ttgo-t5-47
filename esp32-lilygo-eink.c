// ESP32 application which downloads a text file into memory and prints it to the console.

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_http_client.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "credentials.h"

#include "freertos/semphr.h"
#include "epd_driver.h"

#include "font/firasans.h"

static SemaphoreHandle_t downloadSemaphore;
uint8_t *framebuffer = NULL;
int is_data_received = 0;

// TAG for loggin
static const char *TAG = "esp32-lilygo-eink";
static SemaphoreHandle_t displayUpdateSemaphore = NULL;

// PHPSESSID
char *PHPSESSID;


typedef struct {
    char text[1024];  // Adjust size as needed
} DisplayMessage;

static QueueHandle_t displayQueue = NULL;
#define MAX_LINE_LENGTH 50
#define MAX_HTTP_OUTPUT_BUFFER 4096
void split_into_lines_and_append(char *output, const char *text) {
    while (*text) {
        size_t line_len = strlen(text) < MAX_LINE_LENGTH ? strlen(text) : MAX_LINE_LENGTH;
        strncat(output, text, line_len);
        strcat(output, "\n");
        text += line_len;
    }
}

char* convertIso88592ToUtf8(const char* input) {
    if (!input) return NULL;

    size_t len = strlen(input);
    // In the worst case, each character could use up to 2 bytes in UTF-8.
    char* output = (char*)malloc(2 * len + 1);
    if (!output) {
        return NULL;  // Memory allocation failed.
    }

    size_t outIndex = 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)input[i];
        if (c >= 0xA0) {
            switch (c) {
                // Lowercase Czech characters
                case 0xE1:  // á
                    output[outIndex++] = 0xC3;
                    output[outIndex++] = 0xA1;
                    break;
                case 0xE9:  // é
                    output[outIndex++] = 0xC3;
                    output[outIndex++] = 0xA9;
                    break;

                // Add cases for á, í, ó, ú, ý, č, ď, ě, ň, ř, š, ť, ž, ů
                case 0xED:  // í
                    output[outIndex++] = 0xC3;
                    output[outIndex++] = 0xAD;
                    break;
                case 0xF3:  // ó
                    output[outIndex++] = 0xC3;
                    output[outIndex++] = 0xB3;
                    break;
                case 0xFA:  // ú
                    output[outIndex++] = 0xC3;
                    output[outIndex++] = 0xBA;
                    break;
                case 0xFD:  // ý
                    // output[outIndex++] = 0xC3;
                    // output[outIndex++] = 0xBD;
                    output[outIndex++] = 0x79;
                    break;
                case 0xE8:  // č
                    // output[outIndex++] = 0xC4;
                    // output[outIndex++] = 0x8D;
                    output[outIndex++] = 0x63;
                    break;
                case 0xEF:  // ď
                    output[outIndex++] = 0xC4;
                    output[outIndex++] = 0x8F;
                    break;
                case 0xEC:  // ě
                    // output[outIndex++] = 0xC4;
                    // output[outIndex++] = 0x9B;
                    output[outIndex++] = 0x65;
                    break;
                case 0xF2:  // ň
                    output[outIndex++] = 0xC5;
                    output[outIndex++] = 0x88;
                    break;
                case 0xF8:  // ř
                    // output[outIndex++] = 0xC5;
                    // output[outIndex++] = 0x99;
                    output[outIndex++] = 0x72;
                    break;
                case 0xB9:  // š
                    // output[outIndex++] = 0xC5;
                    // output[outIndex++] = 0xA1;
                    output[outIndex++] = 0x73;
                    break;
                case 0xF4:  // ť
                    // output[outIndex++] = 0xC5;
                    // output[outIndex++] = 0xA5;
                    output[outIndex++] = 0x74;
                    break;
                case 0xBE:  // ž
                    // output[outIndex++] = 0xC5;
                    // output[outIndex++] = 0xBE;
                    output[outIndex++] = 0x7A;
                    break;
                case 0xF9:  // ů
                    output[outIndex++] = 0xC5;
                    output[outIndex++] = 0xAF;
                    break;
                // Uppercase Czech characters
                case 0xC1:  // Á
                    output[outIndex++] = 0xC3;
                    output[outIndex++] = 0x81;
                    break;
                case 0xC9:  // É
                    output[outIndex++] = 0xC3;
                    output[outIndex++] = 0x89;
                    break;
                // Add cases for Í, Ó, Ú, Ý, Č, Ď, Ě, Ň, Ř, Š, Ť, Ž, Ů
                case 0xCD:  // Í
                    output[outIndex++] = 0xC3;
                    output[outIndex++] = 0x8D;
                    break;
                case 0xD3:  // Ó
                    output[outIndex++] = 0xC3;
                    output[outIndex++] = 0x93;
                    break;
                case 0xDA:  // Ú
                    output[outIndex++] = 0xC3;
                    output[outIndex++] = 0x9A;
                    break;
                case 0xDD:  // Ý
                    output[outIndex++] = 0xC3;
                    output[outIndex++] = 0x9D;
                    break;
                case 0xC8:  // Č
                    // output[outIndex++] = 0xC4;
                    // output[outIndex++] = 0x8C;
                    output[outIndex++] = 0x43;
                    break;
                case 0xCF:  // Ď
                    // output[outIndex++] = 0xC4;
                    // output[outIndex++] = 0x8E;
                    output[outIndex++] = 0x44;
                    break;
                case 0xC6:  // Ě
                    output[outIndex++] = 0xC4;
                    output[outIndex++] = 0x9A;
                    break;
                case 0xD2:  // Ň
                    // output[outIndex++] = 0xC5;
                    // output[outIndex++] = 0x87;
                    output[outIndex++] = 0x4E;
                    break;
                case 0xD8:  // Ř
                    // output[outIndex++] = 0xC5;
                    // output[outIndex++] = 0x98;
                    output[outIndex++] = 0x52;
                    break;
                case 0xA9:  // Š
                    // output[outIndex++] = 0xC5;
                    // output[outIndex++] = 0xA0;
                    output[outIndex++] = 0x53;
                    break;
                case 0xD4:  // Ť
                    // output[outIndex++] = 0xC5;
                    // output[outIndex++] = 0xA4;
                    output[outIndex++] = 0x54;
                    break;
                case 0xDE:  // Ž
                    // output[outIndex++] = 0xC5;
                    // output[outIndex++] = 0xBD;
                    output[outIndex++] = 0x5A;
                    break;

                // Continue adding cases for all other Czech special characters...

                default:
                    // For characters that do not have a special mapping,
                    // assume they are directly translatable to UTF-8.
                    output[outIndex++] = c;
                    break;
            }
        } else {
            // For ASCII characters, ISO-8859-2 is the same as UTF-8.
            output[outIndex++] = input[i];
        }
    }
    output[outIndex] = '\0';  // Null-terminate the output string.
    return output;
}


void parse_html_content(const char* html_content, char* output, size_t max_len) {
    memset(output, 0, max_len);

    const char* table_start = strstr(html_content, "<table cellpadding=\"2\" cellspacing=\"0\" class=\"ON\" style=\"width: 550px;\">");
    if (!table_start) {
        strncpy(output, "Table not found", max_len - 1);
        return;
    }

    const char* caption_start = strstr(table_start, "<caption class=\"bold_text\"");
    if (!caption_start) {
        strncpy(output, "Caption not found", max_len - 1);
        return;
    }

    if (!strstr(caption_start, "Seznam zadan")) {
        strncpy(output, "Incorrect table", max_len - 1);
        return;
    }

    const char* row_start = strstr(caption_start, "<tr");
    while (row_start && (row_start = strstr(row_start, "<tr"))) {
        const char* next_row_start = strstr(row_start + 1, "<tr");  // Find the start of the next row

        const char* cell_start = strstr(row_start, "<td");
        while (cell_start && (cell_start = strstr(cell_start, "<td")) && (!next_row_start || cell_start < next_row_start)) {
            const char* text_start = strstr(cell_start, ">");
            const char* text_end = strstr(text_start, "</td>");

            size_t text_length = text_end - text_start - 1;
            if (text_length > max_len - strlen(output) - 1) {
                text_length = max_len - strlen(output) - 1;  // Truncate if necessary
            }

            strncat(output, text_start + 1, text_length);
            strncat(output, " | ", 4);  // Separate cell contents with space

            cell_start = strstr(text_end, "<td");  // Move to the next cell
            if (cell_start && next_row_start && cell_start > next_row_start) {
                break;  // Exit if the next cell is in the next row
            }
        }

        strncat(output, "|\n\n", 4);  // Add a newline after each row
        row_start = next_row_start;  // Move to the next row
    }
}



void draw_text(char *text) {
    printf("Drawing text: %s\n", text);
    epd_clear();

    int cursor_x = 10;
    int line_height = 40;
    int cursor_y = line_height;

    char *line = strtok(text, "\n");  // Split the text by newline characters
    while (line != NULL) {
        printf("Drawing line: %s\n", line);
        writeln((GFXfont *)&FiraSans, line, &cursor_x, &cursor_y, NULL);
        cursor_y += line_height;  // Move to the next line
        cursor_x = 10;  // Reset the x-coordinate
        line = strtok(NULL, "\n");  // Get the next line
    }

    // epd_draw_grayscale_image(epd_full_screen(), framebuffer);
    epd_poweroff();
}


void display_task(void *pvParameter) {
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    DisplayMessage msg;
    u_int32_t buffer_size = sizeof(uint8_t) * EPD_WIDTH * EPD_HEIGHT / 2 ;
    printf("Buffer size: %ld", buffer_size);
    framebuffer = (uint8_t *)heap_caps_calloc(1, buffer_size, MALLOC_CAP_SPIRAM);
    if (!framebuffer) {
        printf("Failed to allocate framebuffer");
        return;
    }
    memset(framebuffer, 0xFF, buffer_size);
    printf("Initialize EPD");

    while (1) {
        if (xQueueReceive(displayQueue, &msg, portMAX_DELAY) == pdTRUE) {
            ESP_LOGI(TAG, "Updating display with received text...");
            printf("Received message\n");
            epd_init();

            epd_poweron();
            epd_clear();

            // Now 'msg.text' contains the string to be displayed
            

            draw_text(msg.text);  // Modify 'draw_text' to accept a string parameter
                

        }
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

static char* html_content = NULL;

void allocate_html_buffer() {
    // Allocate 25 KB in PSRAM
    html_content = heap_caps_malloc(30000, MALLOC_CAP_SPIRAM);
    if (!html_content) {
        ESP_LOGE(TAG, "Failed to allocate HTML buffer in PSRAM");
    }
}

void free_html_buffer() {
    if (html_content) {
        heap_caps_free(html_content);
        html_content = NULL;
    }
}


void replace_html_entities(char* text) {
    char* nbsp = "&nbsp;";
    char space = ' ';
    char* result = (char*)malloc(strlen(text) + 1);  // Allocate enough memory
    if (!result) {
        printf("Memory allocation failed\n");
        return;
    }

    char *current_pos = text;
    char *next_token = NULL;
    result[0] = '\0';

    while ((next_token = strstr(current_pos, nbsp)) != NULL) {
        // Copy up to the entity
        strncat(result, current_pos, next_token - current_pos);
        // Replace entity with a space
        strncat(result, &space, 1);
        // Move past the entity in the source string
        current_pos = next_token + strlen(nbsp);
    }

    // Append any remaining part of the string
    strcat(result, current_pos);

    // Copy back to original string
    strcpy(text, result);
    free(result);
}


void format_text_for_display(char** input_text) {
    size_t input_len = strlen(*input_text);
    // Allocate memory considering the worst-case scenario where we insert a newline after every 50 characters
    char* formatted_text = (char*)malloc(input_len + input_len / 50 + 1); // Extra space for newlines and null-terminator

    if (!formatted_text) {
        printf("Failed to allocate memory for formatted text.\n");
        return;
    }

    size_t formatted_index = 0;
    size_t chunk_size = 50;  // Set the chunk size to 50 characters

    for (size_t i = 0; i < input_len; i += chunk_size) {
        if (i + chunk_size > input_len) {
            chunk_size = input_len - i;
        }

        // Copy chunks of text and add a newline character
        strncpy(formatted_text + formatted_index, *input_text + i, chunk_size);
        formatted_index += chunk_size;

        // Add a newline character if it's not the end of the text
        if (i + chunk_size < input_len) {
            formatted_text[formatted_index++] = '\n';
        }
    }

    formatted_text[formatted_index] = '\0';  // Null-terminate the formatted string

    // Replace the original text with the formatted text
    free(*input_text);  // Free the original text memory
    *input_text = formatted_text;  // Point to the new formatted text
}


esp_err_t http_event_handler(esp_http_client_event_t *evt) {
    switch(evt->event_id) {
    case HTTP_EVENT_ERROR:
        // ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
        break;
    case HTTP_EVENT_ON_CONNECTED:
        // ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
        break;
    case HTTP_EVENT_HEADER_SENT:
        // ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
        break;
        case HTTP_EVENT_ON_HEADER: {
            printf("Header: %s = %s\n", evt->header_key, evt->header_value);

            // Check for the Set-Cookie header and extract PHPSESSID
            if (strcasecmp(evt->header_key, "Set-Cookie") == 0 && strstr(evt->header_value, "PHPSESSID=")) {
                const char* sessionIdStart = strstr(evt->header_value, "PHPSESSID=") + 10; // Length of "PHPSESSID=" is 10
                printf("Session ID start: %s\n", sessionIdStart);
                if (sessionIdStart) {
                    const char* sessionIdEnd = strchr(sessionIdStart, ';');
                    printf("Session ID end: %s\n", sessionIdEnd);
                    if (sessionIdEnd) {
                        size_t len = sessionIdEnd - sessionIdStart;
                        PHPSESSID = malloc(len + 1);
                        strncpy(PHPSESSID, sessionIdStart, len);
                        PHPSESSID[len] = '\0';
                        printf("Extracted PHPSESSID: %s\n", PHPSESSID);
                    }
                }
            }
            break;
        }
case HTTP_EVENT_ON_DATA: {
    if (!esp_http_client_is_chunked_response(evt->client) || is_data_received > 0) {
        if (!html_content) {
            ESP_LOGE(TAG, "HTML buffer not allocated");
        } else {
            size_t current_len = strlen(html_content);
            size_t available_space = 29000 - current_len - 1; // Buffer size minus current length minus null terminator
            size_t copy_length = evt->data_len < available_space ? evt->data_len : available_space;

            strncat(html_content, (char*)evt->data, copy_length);

            if (evt->data_len >= available_space) {
                ESP_LOGW(TAG, "HTML content is too large, truncating");
            }
        }
    }
    break;
}

case HTTP_EVENT_ON_FINISH: {
    printf("HTTP_EVENT_ON_FINISH\n");
    is_data_received++;
    // if (html_content && strlen(html_content) > 0 && is_data_received == 1) {
    //     // Find PHPSESSID in html header
    //     char *session_id = strstr(html_content, "PHPSESSID");
    //     if (session_id) {
    //         char *session_id_end = strstr(session_id, ";");
    //         if (session_id_end) {
    //             char session_id[100] = {0};
    //             PHPSESSID = malloc(session_id_end - session_id);
    //             strncpy(PHPSESSID, session_id, session_id_end - session_id);
    //             printf("Session ID: %s\n", session_id);
    //         }
    //     } else {
    //         printf("Session ID not found\n");
    //     }
    // }
    if (html_content && strlen(html_content) > 0 && is_data_received == 3) {


    // printf("HTML content: %s\n", html_content);
        // Buffer for the parsed content
        char parsed_content[1024] = {0}; // Adjust size as needed

        // Parse HTML content and extract the data
        parse_html_content(html_content, parsed_content, sizeof(parsed_content));
        printf("Parsed content: %s\n", parsed_content);

        // Now convert the extracted (parsed) text to UTF-8.
        char *utf8_content = convertIso88592ToUtf8(parsed_content);
        printf("UTF-8 content: %s\n", utf8_content);
        if (utf8_content) {
            replace_html_entities(utf8_content);
            format_text_for_display(&utf8_content);  // Format the text for display

            // Prepare the display message
            DisplayMessage msg;
            memset(&msg, 0, sizeof(msg));
            strncpy(msg.text, utf8_content, sizeof(msg.text) - 1);
            free(utf8_content); // Free the memory allocated for UTF-8 conversion

            // Send the message to the display task
            if (strlen(msg.text) == 0) {
                strncpy(msg.text, "No data found", sizeof(msg.text) - 1);
            }
            xQueueSend(displayQueue, &msg, portMAX_DELAY);
        } else {
            printf("UTF-8 conversion failed.\n");
        }
    }
    break;
}
    case HTTP_EVENT_DISCONNECTED:
        // ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
        break;
    case HTTP_EVENT_REDIRECT:
        // ESP_LOGD(TAG, "HTTP_EVENT_REDIRECT");
        break;
    }
    return ESP_OK;
}


void http_auth_request() {
    char output_buffer[MAX_HTTP_OUTPUT_BUFFER];  // Buffer to store response
    esp_http_client_config_t config = {
        .url = "https://aes.zskaminky.cz/auth/?u=https%3A%2F%2Faes.zskaminky.cz%2Frodic%2Findex.php%3Fl%3Dcs&a=3&l=cs",
        // .url = "https://georgik.rocks/tmp/data.html",
        .event_handler = http_event_handler,
        .user_agent = "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/121.0.0.0 Safari/537.36",
        .cert_pem = GeorgikPem,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);

    // First GET request to establish a session
    esp_http_client_perform(client);

    // Authentication POST request
    int postDataLength = snprintf(NULL, 0, "username=%s&pwd=%s&submit=", UserLogin, UserPassword) + 1;

    // Allocate memory for the post data string
    char *post_data = (char *)malloc(postDataLength);

    // Construct the post data string with the credentials
    if (post_data) {
        snprintf(post_data, postDataLength, "username=%s&pwd=%s&submit=", UserLogin, UserPassword);
    } else {
        ESP_LOGE(TAG, "Failed to allocate memory for post data");
    }

char cookie_header[256]; // Buffer size should be enough to hold the entire cookie header
snprintf(cookie_header, sizeof(cookie_header), "PHPSESSID=%s", PHPSESSID);
    esp_http_client_set_url(client, "https://aes.zskaminky.cz/auth/?u=https%3A%2F%2Faes.zskaminky.cz%2Frodic%2Findex.php%3Fl%3Dcs&a=3&l=cs");
    // esp_http_client_set_url(client, "https://georgik.rocks/tmp/data.html");
    esp_http_client_set_method(client, HTTP_METHOD_POST);
printf("Cookie header: %s\n", cookie_header);
printf("Post data: %s\n", post_data);
  esp_http_client_set_header(client, "Cookie", cookie_header);
    esp_http_client_set_header(client, "Content-Type", "application/x-www-form-urlencoded");
    esp_http_client_set_post_field(client, post_data, strlen(post_data));
    esp_http_client_perform(client);

    // esp_http_client_set_url(client, "https://aes.zskaminky.cz/rodic/index.php?l=cs");
    // // esp_http_client_set_url(client, "https://georgik.rocks/tmp/data.html");
    // esp_http_client_set_method(client, HTTP_METHOD_GET);
    // esp_http_client_set_header(client, "Cookie", cookie_header);
    // // esp_http_client_set_header(client, "Content-Type", "application/x-www-form-urlencoded");
    // // esp_http_client_set_post_field(client, post_data, strlen(post_data));
    // esp_http_client_perform(client);


    // Cleanup
    esp_http_client_cleanup(client);
    free(post_data);

}
// #include "esp_crt_bundle.h" // Include this header for certificate bundle

void download_text_file(const char *url) {
    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event_handler,
        // .cert_pem = GeorgikPem,
        .cert_pem = KaminkyPem,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    esp_err_t err = esp_http_client_perform(client);

    // if (err == ESP_OK) {
    //     ESP_LOGI(TAG, "HTTPS Status = %d, content_length = %d",
    //              esp_http_client_get_status_code(client),
    //              esp_http_client_get_content_length(client));
    // } else {
    //     ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
    // }

    esp_http_client_cleanup(client);
}

static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        ESP_LOGI(TAG, "WiFi connect initiated...");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
        ESP_LOGI(TAG, "Disconnected. Attempting to reconnect...");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));

        // Instead of calling download_text_file, give the semaphore
        xSemaphoreGive(downloadSemaphore);
    }
}



void wifi_init_sta(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init()); // Initializes the TCP/IP stack

    // Note: The esp_event_loop_create_default() call has been removed from here.

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    // ESP_LOGI(TAG, "wifi_init_sta finished.");
    // ESP_LOGI(TAG, "connect to ap SSID:%s password:%s", WIFI_SSID, "******");
}


void app_main(void) {
    esp_log_level_set("*", ESP_LOG_VERBOSE);
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Initialize the semaphores
    downloadSemaphore = xSemaphoreCreateBinary();
    displayUpdateSemaphore = xSemaphoreCreateBinary();
    displayQueue = xQueueCreate(10, sizeof(DisplayMessage));  
         // Check for successful semaphore creation
    if (downloadSemaphore == NULL || displayUpdateSemaphore == NULL) {
        ESP_LOGE(TAG, "Failed to create semaphores");
        // Handle semaphore creation failure (e.g., reset or halt)
    }
    allocate_html_buffer();

     xTaskCreate(&display_task, "display_task", 32000, NULL, 5, NULL);

    wifi_init_sta();

    // Wait indefinitely for the semaphore to be given
    while (true) {
        if (xSemaphoreTake(downloadSemaphore, portMAX_DELAY) == pdTRUE) {
            ESP_LOGI(TAG, "Semaphore given. Initiating download...");
            // download_text_file("https://georgik.rocks/tmp/jidelnicek.txt");
            http_auth_request();
        }
        vTaskDelay(1000 / portTICK_PERIOD_MS);

    }
}
