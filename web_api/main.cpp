#include "common/globals.h"
#include "assembler/assembler.h"
#include "processor/processor_manager.h"
#include "processor/rv5s/rv5s_branch_predictor.h"
#include "processor/processor_types.h"
#include "utils/utils.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QHostAddress>
#include <QUrl>
#include <QUrlQuery>
#include <QTcpServer>
#include <QTcpSocket>

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace {
using namespace Kites;

QString hex64(uint64_t value) {
    return QStringLiteral("0x") + QString::number(value, 16).rightJustified(16, QLatin1Char('0'));
}

uint64_t parseAddress(const QString &text) {
    QString value = text.trimmed();
    bool ok = false;
    int base = 10;
    if (value.startsWith("0x", Qt::CaseInsensitive)) {
        value = value.mid(2);
        base = 16;
    }
    const qulonglong parsed = value.toULongLong(&ok, base);
    if (!ok) throw std::invalid_argument("Invalid memory address");
    return static_cast<uint64_t>(parsed);
}

QString writePolicyName(WritePolicy policy) {
    return policy == WritePolicy::WriteBack ? QStringLiteral("write-back") : QStringLiteral("write-through");
}

QString allocationPolicyName(AllocationPolicy policy) {
    return policy == AllocationPolicy::WriteAllocate ? QStringLiteral("write-allocate") : QStringLiteral("no-write-allocate");
}

QString replacementPolicyName(ReplacementPolicy policy) {
    if (policy == ReplacementPolicy::FIFO) return QStringLiteral("FIFO");
    if (policy == ReplacementPolicy::Custom) return QStringLiteral("Custom");
    return QStringLiteral("LRU");
}

Cache *cacheForLevel(ProcessorManager &manager, const QString &level) {
    auto *memory = manager.getMemoryController();
    if (level == "instruction") return memory->getInstructionCache();
    if (level == "l1") return memory->getL1Cache();
    if (level == "l2") return memory->getL2Cache();
    throw std::invalid_argument("Unknown cache level");
}

QJsonObject cacheSnapshot(Cache *cache, const QString &id, const QString &name) {
    const CacheConfig config = cache->getConfig();
    QJsonArray lines;
    for (size_t set = 0; set < cache->getSetCount(); ++set) {
        for (size_t way = 0; way < cache->getWayCount(); ++way) {
            const CacheLine &line = cache->getCacheLine(set, way);
            QString data;
            data.reserve(static_cast<qsizetype>(line.data.size() * 2));
            for (uint8_t byte : line.data)
                data += QString::number(byte, 16).rightJustified(2, QLatin1Char('0'));
            lines.append(QJsonObject{{"set", static_cast<qint64>(set)},
                                     {"way", static_cast<qint64>(way)},
                                     {"tag", QStringLiteral("0x") + QString::number(line.tag, 16)},
                                     {"valid", line.valid}, {"dirty", line.dirty}, {"data", data}});
        }
    }
    const QJsonObject configObject{{"setCount", static_cast<qint64>(config.lineCount)},
                                   {"lineSizeInBytes", static_cast<qint64>(config.lineSizeInBytes)},
                                   {"wayCount", static_cast<qint64>(config.wayCount)},
                                   {"writePolicy", writePolicyName(config.writePolicy)},
                                   {"allocationPolicy", allocationPolicyName(config.allocationPolicy)},
                                   {"replacementPolicy", replacementPolicyName(config.replacementPolicy)}};
    const QJsonObject statsObject{{"hits", static_cast<qint64>(cache->getHitCount())},
                                  {"misses", static_cast<qint64>(cache->getMissCount())},
                                  {"writeBacks", static_cast<qint64>(cache->getWriteBackCount())},
                                  {"hitRate", cache->getHitRate()},
                                  {"missRate", cache->getMissRate()},
                                  {"sizeBytes", static_cast<qint64>(cache->getCacheSizeInBytes())}};
    return QJsonObject{{"id", id}, {"name", name}, {"config", configObject},
                       {"stats", statsObject}, {"lines", lines}};
}

ProcessorType parseVm(const QString &name) {
    if (name == "rvss" || name.isEmpty()) return ProcessorType::RVSS;
    if (name == "rv5-nh-nf") return ProcessorType::RV5Stage_NH_NF;
    if (name == "rv5-h-nf") return ProcessorType::RV5Stage_H_NF;
    if (name == "rv5-nh-f") return ProcessorType::RV5Stage_NH_F;
    if (name == "rv5-h-f") return ProcessorType::RV5Stage_H_F;
    throw std::invalid_argument("Unknown Kites VM type");
}

BranchPredictorType parseBranchPredictor(const QString &name) {
    if (name.isEmpty() || name == "2bit") return BranchPredictorType::TwoBitBHT;
    if (name == "static-nt" || name == "none") return BranchPredictorType::StaticNotTaken;
    throw std::invalid_argument("Unknown branch predictor type");
}

QString branchPredictorToString(BranchPredictorType type) {
    return type == BranchPredictorType::TwoBitBHT ? QStringLiteral("2bit")
                                                  : QStringLiteral("static-nt");
}

QString firstParseError() {
    QFile file(QString::fromStdString(globals::errors_dump_file_path.string()));
    if (!file.open(QIODevice::ReadOnly)) return {};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    const QJsonArray errors = doc.object().value("errors").toArray();
    if (errors.isEmpty()) return {};
    const QJsonObject first = errors.first().toObject();
    const int line = first.value("line").toInt();
    const QString message = first.value("message").toString();
    if (message.isEmpty()) return {};
    return line > 0 ? QStringLiteral("line %1: %2").arg(line).arg(message) : message;
}

QString bhtStateLabel(uint8_t state) {
    switch (state) {
    case 0: return QStringLiteral("00 (Strongly Not Taken)");
    case 1: return QStringLiteral("01 (Weakly Not Taken)");
    case 2: return QStringLiteral("10 (Weakly Taken)");
    case 3: return QStringLiteral("11 (Strongly Taken)");
    default: return QStringLiteral("Unknown");
    }
}

QString processorName(ProcessorType type) {
    switch (type) {
    case ProcessorType::RVSS: return "RVSS single-cycle";
    case ProcessorType::RV5Stage_NH_NF: return "RV5 pipeline — no hazard detection, no forwarding";
    case ProcessorType::RV5Stage_H_NF: return "RV5 pipeline — hazard detection, no forwarding";
    case ProcessorType::RV5Stage_NH_F: return "RV5 pipeline — no hazard detection, forwarding";
    case ProcessorType::RV5Stage_H_F: return "RV5 pipeline — hazard detection, forwarding";
    default: return "Kites processor";
    }
}

const char *stageName(int index) {
    static const char *names[] = {"IF", "ID", "EX", "MEM", "WB"};
    return index >= 0 && index < 5 ? names[index] : "?";
}

QString hazardKindName(HazardKind kind) {
    switch (kind) {
    case HazardKind::Data: return "data";
    case HazardKind::LoadUse: return "load-use";
    case HazardKind::Control: return "control";
    default: return "none";
    }
}

QString pcStringFor(uint64_t pc) {
    return pc == INVALID_PC ? QString() : hex64(pc);
}

QJsonObject hazardJson(const HazardReport &hazard) {
    const auto pcString = [](uint64_t pc) { return pc == INVALID_PC ? QString() : hex64(pc); };
    return {{"active", hazard.active()},
            {"type", hazardKindName(hazard.kind)},
            {"stalled", hazard.stalled},
            {"forwarded", hazard.forwarded},
            {"stalls", hazard.stallCycles},
            {"sourcePc", pcString(hazard.sourcePc)},
            {"consumerPc", pcString(hazard.consumerPc)},
            {"register", static_cast<int>(hazard.registerIndex)},
            {"fpr", hazard.isFpr}};
}

class WebApi final : public QObject {
    Q_OBJECT
public:
    explicit WebApi(QObject *parent = nullptr)
        : QObject(parent), manager_(nullptr, ProcessorType::RVSS) {
        manager_.setStepDelay(1);
        connect(&manager_, &ProcessorManager::runErrorSignal, this,
                [this](const QString &message, int) { lastRunError_ = message; });
        connect(&server_, &QTcpServer::newConnection, this, [this]() {
            while (server_.hasPendingConnections()) {
                QTcpSocket *socket = server_.nextPendingConnection();
                socket->setProperty("requestBuffer", QByteArray{});
                connect(socket, &QTcpSocket::readyRead, this, [this, socket]() { readRequest(socket); });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
    }

    bool listen(quint16 port) { return server_.listen(QHostAddress::Any, port); }

private:
    QTcpServer server_;
    ProcessorManager manager_;
    bool programLoaded_{false};
    bool programFinished_{false};
    QString lastRunError_;

    void readRequest(QTcpSocket *socket) {
        QByteArray buffer = socket->property("requestBuffer").toByteArray();
        buffer += socket->readAll();
        socket->setProperty("requestBuffer", buffer);
        const qsizetype headerEnd = buffer.indexOf("\r\n\r\n");
        if (headerEnd < 0) return;
        const QByteArray headers = buffer.left(headerEnd);
        int contentLength = 0;
        for (const QByteArray &line : headers.split('\n')) {
            const QByteArray lower = line.trimmed().toLower();
            if (lower.startsWith("content-length:")) contentLength = lower.mid(15).trimmed().toInt();
        }
        const qsizetype bodyStart = headerEnd + 4;
        if (buffer.size() < bodyStart + contentLength) return;
        const QByteArray requestLine = headers.left(headers.indexOf("\r\n"));
        const QList<QByteArray> parts = requestLine.split(' ');
        if (parts.size() < 2) { send(socket, 400, {{"error", "Malformed request"}}); return; }
        const QByteArray method = parts[0];
        const QUrl url = QUrl::fromEncoded(parts[1]);
        const QByteArray body = buffer.mid(bodyStart, contentLength);
        try {
            if (method == "OPTIONS") { send(socket, 204, {}); return; }
            QJsonObject result;
            int status = 200;
            if (method == "GET" && url.path() == "/api/health") {
                result = {{"ok", true}, {"engine", "Kites"}, {"vm", "rvss"}, {"programLoaded", programLoaded_}, {"programComplete", programFinished_}};
            } else if (method == "POST" && url.path() == "/api/diagnostics") {
                const auto json = parseJson(body);
                std::istringstream source(json.value("source").toString().toStdString());
                const auto diagnostics = getDiagnostics(source, "<editor>");
                QJsonArray items;
                for (const auto &diagnostic : diagnostics)
                    items.append(QJsonObject{{"line", static_cast<int>(diagnostic.line)},
                                             {"column", static_cast<int>(diagnostic.column)},
                                             {"message", QString::fromStdString(diagnostic.message)}});
                result = {{"diagnostics", items}};
            } else if (method == "GET" && url.path() == "/api/cache") {
                QJsonArray levels;
                levels.append(cacheSnapshot(cacheForLevel(manager_, "instruction"), "instruction", "Instruction Cache"));
                levels.append(cacheSnapshot(cacheForLevel(manager_, "l1"), "l1", "L1 Data Cache"));
                levels.append(cacheSnapshot(cacheForLevel(manager_, "l2"), "l2", "L2 Unified Cache"));
                result = {{"levels", levels}};
            } else if (method == "POST" && url.path() == "/api/cache/config") {
                const auto json = parseJson(body);
                const int sets = json.value("setCount").toInt();
                const int lineSize = json.value("lineSizeInBytes").toInt();
                const int ways = json.value("wayCount").toInt();
                if (sets < 1 || sets > 64 || lineSize < 4 || lineSize > 64 || ways < 1 || ways > 8 ||
                    !std::has_single_bit(static_cast<unsigned int>(sets)) ||
                    !std::has_single_bit(static_cast<unsigned int>(lineSize)) ||
                    !std::has_single_bit(static_cast<unsigned int>(ways)))
                    throw std::invalid_argument("Cache geometry must use supported power-of-two sizes");
                const QString write = json.value("writePolicy").toString("write-through");
                const QString allocation = json.value("allocationPolicy").toString("write-allocate");
                const QString replacement = json.value("replacementPolicy").toString("LRU");
                if ((write != "write-through" && write != "write-back") ||
                    (allocation != "write-allocate" && allocation != "no-write-allocate") ||
                    (replacement != "LRU" && replacement != "FIFO"))
                    throw std::invalid_argument("Unsupported cache policy");
                CacheConfig config{
                    .lineCount = static_cast<size_t>(sets),
                    .lineSizeInBytes = static_cast<size_t>(lineSize),
                    .wayCount = static_cast<size_t>(ways),
                    .writePolicy = write == "write-back" ? WritePolicy::WriteBack : WritePolicy::WriteThrough,
                    .allocationPolicy = allocation == "write-allocate" ? AllocationPolicy::WriteAllocate : AllocationPolicy::NoWriteAllocate,
                    .replacementPolicy = replacement == "FIFO" ? ReplacementPolicy::FIFO : ReplacementPolicy::LRU};
                Cache *cache = cacheForLevel(manager_, json.value("level").toString());
                cache->reconfigure(config);
                result = cacheSnapshot(cache, json.value("level").toString(),
                    json.value("level").toString() == "instruction" ? "Instruction Cache" :
                    json.value("level").toString() == "l1" ? "L1 Data Cache" : "L2 Unified Cache");
            } else if (method == "POST" && url.path() == "/api/program/assemble") {
                const auto json = parseJson(body);
                const QString vm = json.value("vm").toString("rvss");
                const QString predictor = json.value("branchPredictor").toString("2bit");
                if (parseVm(vm) != manager_.getProcessorType()) manager_.changeProcessor(parseVm(vm));
                manager_.setBranchPredictor(parseBranchPredictor(predictor));
                manager_.setStepDelay(1);
                const std::string source = json.value("source").toString().toStdString();
                if (source.empty()) throw std::invalid_argument("Assembly source is empty");
                manager_.reset();
                programLoaded_ = false;
                programFinished_ = false;
                try {
                    manager_.loadProgram(source);
                } catch (const std::exception &error) {
                    std::istringstream diagnosticSource(source);
                    QJsonArray items;
                    for (const auto &diagnostic : getDiagnostics(diagnosticSource, "<editor>"))
                        items.append(QJsonObject{{"line", static_cast<int>(diagnostic.line)},
                                                 {"column", static_cast<int>(diagnostic.column)},
                                                 {"message", QString::fromStdString(diagnostic.message)}});
                    QString errorMessage = QString::fromUtf8(error.what());
                    const QString detail = firstParseError();
                    if (!detail.isEmpty() && errorMessage.contains("Failed to "))
                        errorMessage = QStringLiteral("Assembly failed at %1").arg(detail);
                    send(socket, 400, {{"error", errorMessage}, {"diagnostics", items}});
                    return;
                }
                programLoaded_ = true;
                programFinished_ = false;
                result = state();
                QJsonArray instructions;
                const auto &program = manager_.getCurrentProgram();
                for (size_t i = 0; i < program.text_buffer.size(); ++i) {
                    const auto sourceLine = program.instruction_number_line_number_mapping.find(
                        static_cast<unsigned int>(i));
                    if (sourceLine == program.instruction_number_line_number_mapping.end() ||
                        i >= program.intermediate_code.size()) continue;
                    std::ostringstream instruction;
                    instruction << program.intermediate_code[i].first;
                    instructions.append(QJsonObject{
                        {"address", hex64(static_cast<uint64_t>(i) * 4)},
                        {"encoding", QStringLiteral("0x") + QString::number(program.text_buffer[i], 16).rightJustified(8, QLatin1Char('0'))},
                        {"instruction", QString::fromStdString(instruction.str())},
                        {"sourceLine", static_cast<int>(sourceLine->second)}});
                }
                result.insert("instructions", instructions);
                result.insert("assembled", true);
                result.insert("message", "Program assembled and loaded by Kites");
            } else if (method == "POST" && url.path() == "/api/simulator/step") {
                requireProgram();
                const auto json = parseJson(body);
                const int count = std::clamp(json.value("count").toInt(1), 1, 1000);
                lastRunError_.clear();
                for (int i = 0; i < count && !programFinished_; ++i) {
                    programFinished_ = manager_.step();
                    if (!lastRunError_.isEmpty()) throw std::runtime_error(lastRunError_.toStdString());
                }
                result = state();
                result.insert("message", programFinished_ ? "Program execution complete" : "Kites executed step request");
            } else if (method == "POST" && url.path() == "/api/simulator/run") {
                requireProgram();
                lastRunError_.clear();
                manager_.run();
                if (!lastRunError_.isEmpty()) throw std::runtime_error(lastRunError_.toStdString());
                programFinished_ = true;
                result = state();
                result.insert("message", "Kites run completed");
            } else if (method == "POST" && url.path() == "/api/simulator/reset") {
                manager_.reset();
                programLoaded_ = false;
                programFinished_ = false;
                result = state();
                result.insert("message", "Simulator reset");
            } else if (method == "GET" && url.path() == "/api/state") {
                result = state();
            } else if (method == "GET" && url.path() == "/api/memory") {
                QUrlQuery query(url);
                const uint64_t start = parseAddress(query.queryItemValue("address", QUrl::FullyDecoded).isEmpty()
                    ? "0x10000000" : query.queryItemValue("address", QUrl::FullyDecoded));
                const uint64_t requested = query.queryItemValue("length").isEmpty() ? 256 : query.queryItemValue("length").toULongLong();
                const uint64_t length = std::clamp<uint64_t>(requested, 1, 4096);
                if (start > std::numeric_limits<uint64_t>::max() - length) throw std::invalid_argument("Memory range overflows address space");
                QJsonArray bytes;
                auto *memory = manager_.getMemoryController();
                for (uint64_t i = 0; i < length; ++i) bytes.append(static_cast<int>(memory->peekByte(start + i)));
                result = {{"address", hex64(start)}, {"length", static_cast<qint64>(length)}, {"bytes", bytes}};
            } else if (method == "POST" && url.path() == "/api/memory/write") {
                const auto json = parseJson(body);
                const uint64_t address = parseAddress(json.value("address").toString());
                const int value = json.value("value").toInt(-1);
                if (value < 0 || value > 255) throw std::invalid_argument("Memory byte must be between 0 and 255");
                manager_.getMemoryController()->writeByte_d(address, static_cast<uint8_t>(value));
                result = {{"ok", true}, {"address", hex64(address)}, {"value", value}};
            } else {
                status = 404;
                result = {{"error", "Endpoint not found"}};
            }
            send(socket, status, result);
        } catch (const std::exception &e) {
            send(socket, 400, {{"error", QString::fromUtf8(e.what())}});
        }
    }

    static QJsonObject parseJson(const QByteArray &body) {
        QJsonParseError error{};
        const QJsonDocument doc = QJsonDocument::fromJson(body, &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject())
            throw std::invalid_argument("Request body must be a JSON object");
        return doc.object();
    }

    void requireProgram() const {
        if (!programLoaded_) throw std::invalid_argument("Assemble a program before executing it");
    }

    QJsonObject state() const {
        const RegisterFile *registers = manager_.getRegisterFile();
        QJsonObject gpr, fpr, csr;
        for (int i = 0; i < 32; ++i) {
            gpr.insert(QString("x%1").arg(i), hex64(registers->ReadGpr(static_cast<size_t>(i))));
            fpr.insert(QString("f%1").arg(i), hex64(registers->ReadFpr(static_cast<size_t>(i))));
        }
        for (const auto &[name, address] : csr_to_address)
            csr.insert(QString::fromStdString(name), hex64(registers->ReadCsr(static_cast<size_t>(address))));
        const ProcessorType type = manager_.getProcessorType();
        const ProcessorState &processorState = manager_.getProcessorState();
        const bool pipelined = type != ProcessorType::RVSS;

        const uint64_t programSize = static_cast<uint64_t>(manager_.getCurrentProgram().text_buffer.size()) * 4;
        QJsonArray stages;
        if (!pipelined) {
            const uint64_t pc = programFinished_ && processorState.lastExecutedPC != INVALID_PC
                ? processorState.lastExecutedPC
                : manager_.getProgramCounter();
            stages.append(QJsonObject{{"stage", "EX"}, {"pc", hex64(pc)}, {"valid", true}});
        } else {
            for (int i = 0; i < 5; ++i) {
                const uint64_t pc = i < static_cast<int>(processorState.programCounters.size())
                    ? processorState.programCounters[static_cast<size_t>(i)]
                    : INVALID_PC;
                const bool valid = pc != INVALID_PC && pc < programSize;
                stages.append(QJsonObject{{"stage", stageName(i)},
                                          {"pc", valid ? hex64(pc) : QString()},
                                          {"valid", valid}});
            }
        }

        QJsonArray btbEntries;
        for (const auto &row : manager_.getBranchPredictorEntries()) {
            btbEntries.append(QJsonObject{{"index", static_cast<int>(row.index)},
                                          {"pc", hex64(row.pc)},
                                          {"target", hex64(row.target)},
                                          {"counter", static_cast<int>(row.state)},
                                          {"stateLabel", bhtStateLabel(row.state)},
                                          {"unconditional", row.is_unconditional}});
        }

        return {{"pc", hex64(manager_.getProgramCounter())}, {"gpr", gpr}, {"fpr", fpr},
                {"csr", csr}, {"cycles", static_cast<qint64>(manager_.getCycles())},
                {"instructionsRetired", static_cast<qint64>(manager_.getInstructionsRetired())},
                {"stallCycles", static_cast<qint64>(manager_.getStallCycles())},
                {"branchMispredictions", static_cast<qint64>(manager_.getBranchMispredictions())},
                {"branchPredictor", branchPredictorToString(manager_.getBranchPredictorType())},
                {"branchPredictions", static_cast<qint64>(manager_.getBranchPredictions())},
                {"btbHits", static_cast<qint64>(manager_.getBTBHits())},
                {"branchAccuracy", manager_.getBranchAccuracy()},
                {"btbEntries", btbEntries},
                {"cpi", manager_.getCPI()}, {"ipc", manager_.getIPC()},
                {"programLoaded", programLoaded_}, {"programComplete", programFinished_},
                {"processor", processorName(type)}, {"pipeline", pipelined},
                {"stages", stages}, {"hazard", hazardJson(processorState.hazard)},
                {"lastExecutedPc", pcStringFor(processorState.lastExecutedPC)}};
    }

    static void send(QTcpSocket *socket, int status, const QJsonObject &object) {
        const QByteArray body = QJsonDocument(object).toJson(QJsonDocument::Compact);
        QByteArray reason = "OK";
        if (status == 204) reason = "No Content";
        else if (status == 400) reason = "Bad Request";
        else if (status == 404) reason = "Not Found";
        QByteArray response = "HTTP/1.1 " + QByteArray::number(status) + " " + reason + "\r\n";
        response += "Content-Type: application/json; charset=utf-8\r\n";
        response += "Access-Control-Allow-Origin: *\r\n";
        response += "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
        response += "Access-Control-Allow-Headers: Content-Type\r\n";
        response += "Connection: close\r\n";
        response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n";
        response += body;
        socket->write(response);
        socket->disconnectFromHost();
    }
};
}

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    Kites::setupVmStateDirectory();
    WebApi api;
    const quint16 port = argc > 1 ? QString::fromLocal8Bit(argv[1]).toUShort() : 8080;
    if (!api.listen(port)) {
        qCritical("Could not start Kites Web API on port %u", port);
        return 1;
    }
    qInfo("Kites Web API listening on http://127.0.0.1:%u", port);
    return app.exec();
}

#include "main.moc"
