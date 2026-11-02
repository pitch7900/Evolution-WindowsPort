#pragma once

#include "pch.h"
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Networking.h>
#include <winrt/Windows.Networking.Sockets.h>
#include <winrt/Windows.Storage.Streams.h>
#include <functional>
#include <memory>
#include <string>

namespace EvolutionWin
{
    /* Écouteur HTTP local loopback conforme à la RFC 8252 (OAuth 2.0 pour applications natives).
     * Ouvre un port local temporaire sur 127.0.0.1 pour intercepter le retour de l'authentification
     * moderne (ex: Google / Gmail) exécutée dans le navigateur système par défaut, sans risque
     * de blocage 'disallowed_useragent'. */
    class OAuth2LoopbackListener : public std::enable_shared_from_this<OAuth2LoopbackListener>
    {
    public:
        using CallbackFunc = std::function<void(winrt::hstring const& full_redirect_uri)>;

        OAuth2LoopbackListener() = default;

        ~OAuth2LoopbackListener()
        {
            Close();
        }

        /* Démarre l'écoute sur 127.0.0.1 avec un port éphémère attribué dynamiquement par le système.
         * Paramètres :
         *   on_callback : fonction rappelée lors de la réception de la redirection avec le code */
        winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> StartAsync(CallbackFunc on_callback)
        {
            m_callback = std::move(on_callback);
            m_listener = winrt::Windows::Networking::Sockets::StreamSocketListener();

            /* Configuration de la réception des connexions entrantes */
            m_listener.ConnectionReceived([weak = weak_from_this()](
                winrt::Windows::Networking::Sockets::StreamSocketListener const&,
                winrt::Windows::Networking::Sockets::StreamSocketListenerConnectionReceivedEventArgs const& args)
            {
                if (auto strong = weak.lock())
                {
                    strong->HandleConnectionAsync(args.Socket());
                }
            });

            /* Liaison sur 127.0.0.1 avec port dynamique */
            winrt::Windows::Networking::HostName loopback_host{ L"127.0.0.1" };
            co_await m_listener.BindEndpointAsync(loopback_host, L"");

            m_port = m_listener.Information().LocalPort();
            m_redirect_uri = L"http://127.0.0.1:" + m_port + L"/oauth2callback";

            co_return m_redirect_uri;
        }

        /* Renvoie l'adresse de redirection complète calculée. */
        winrt::hstring RedirectUri() const
        {
            return m_redirect_uri;
        }

        /* Arrête l'écoute et libère les sockets. */
        void Close()
        {
            if (m_listener)
            {
                try
                {
                    m_listener.Close();
                }
                catch (...) {}
                m_listener = nullptr;
            }
        }

    private:
        winrt::Windows::Foundation::IAsyncAction HandleConnectionAsync(winrt::Windows::Networking::Sockets::StreamSocket socket)
        {
            auto strong = shared_from_this();

            try
            {
                winrt::Windows::Storage::Streams::DataReader reader{ socket.InputStream() };
                reader.InputStreamOptions(winrt::Windows::Storage::Streams::InputStreamOptions::Partial);
                co_await reader.LoadAsync(2048);

                uint32_t available = reader.UnconsumedBufferLength();
                if (available == 0)
                    co_return;

                std::vector<uint8_t> buffer(available);
                reader.ReadBytes(buffer);
                std::string request_str(reinterpret_cast<char const*>(buffer.data()), buffer.size());

                /* Extraction du chemin de la requête GET (ex: GET /oauth2callback?code=... HTTP/1.1) */
                std::string path_and_query;
                auto get_pos = request_str.find("GET ");
                if (get_pos != std::string::npos)
                {
                    auto space_pos = request_str.find(' ', get_pos + 4);
                    if (space_pos != std::string::npos)
                    {
                        path_and_query = request_str.substr(get_pos + 4, space_pos - (get_pos + 4));
                    }
                }

                /* Réponse HTML conviviale au navigateur */
                std::string body = 
                    "<!DOCTYPE html>\r\n"
                    "<html lang=\"fr\">\r\n"
                    "<head>\r\n"
                    "  <meta charset=\"utf-8\">\r\n"
                    "  <title>Connexion réussie</title>\r\n"
                    "  <style>\r\n"
                    "    body { font-family: 'Segoe UI', Arial, sans-serif; background-color: #f3f4f6; display: flex; align-items: center; justify-content: center; height: 100vh; margin: 0; }\r\n"
                    "    .card { background: #ffffff; padding: 40px; border-radius: 12px; box-shadow: 0 4px 16px rgba(0,0,0,0.1); text-align: center; max-width: 440px; }\r\n"
                    "    .icon { font-size: 50px; margin-bottom: 12px; }\r\n"
                    "    h1 { color: #0f766e; font-size: 22px; margin-bottom: 8px; }\r\n"
                    "    p { color: #4b5563; font-size: 14px; line-height: 1.5; margin: 6px 0; }\r\n"
                    "  </style>\r\n"
                    "</head>\r\n"
                    "<body>\r\n"
                    "  <div class=\"card\">\r\n"
                    "    <div class=\"icon\">&#x2705;</div>\r\n"
                    "    <h1>Connexion réussie !</h1>\r\n"
                    "    <p>Votre compte a été authentifié avec succès.</p>\r\n"
                    "    <p>Vous pouvez à présent fermer cet onglet et revenir à <strong>Evolution</strong>.</p>\r\n"
                    "  </div>\r\n"
                    "</body>\r\n"
                    "</html>\r\n";

                std::string http_response =
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: text/html; charset=utf-8\r\n"
                    "Content-Length: " + std::to_string(body.size()) + "\r\n"
                    "Connection: close\r\n\r\n" + body;

                winrt::Windows::Storage::Streams::DataWriter writer{ socket.OutputStream() };
                writer.WriteString(winrt::to_hstring(http_response));
                co_await writer.StoreAsync();
                co_await writer.FlushAsync();
                writer.DetachStream();

                /* Reconstitution de l'URI complète reçue */
                std::wstring wide_path{ path_and_query.begin(), path_and_query.end() };
                std::wstring full_url = L"http://127.0.0.1:" + std::wstring{ m_port } + wide_path;

                /* Notification au rappel puis fermeture */
                if (m_callback)
                {
                    m_callback(winrt::hstring{ full_url });
                }

                Close();
            }
            catch (...)
            {
                Close();
            }
        }

        winrt::Windows::Networking::Sockets::StreamSocketListener m_listener{ nullptr };
        winrt::hstring m_port;
        winrt::hstring m_redirect_uri;
        CallbackFunc m_callback;
    };
}
